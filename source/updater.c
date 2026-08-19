#include "updater.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

// The socket buffer libctru hands to the network stack. It has to be aligned to
// 0x1000 and a multiple of 0x1000, and 1MB is the size every devkitPro socket
// example is written against. Smaller almost certainly works for one connection
// at a time, but "almost certainly" is not worth debugging over wifi on a
// handheld, so this stays at the well-trodden value.
#define SOC_BUFFER_SIZE (0x100000)

// The worker's stack. An mbedtls handshake is not shy with stack, so this is
// deliberately roomier than the 32KB a plain worker would get.
#define WORKER_STACK_SIZE (0x10000)

// Ceiling on the GitHub API response held in memory. A releases payload is a
// few KB; anything past this is a sign something other than the API answered,
// and truncating beats growing a buffer until the heap gives out.
#define API_RESPONSE_MAX (128 * 1024)

// How many times a network step is tried before it is reported as a failure.
//
// This is the answer to "it said it could not update, I pressed it again and it
// worked". The console's wifi stack loses DNS answers, drops TLS handshakes and
// stalls mid-transfer often enough that one attempt is not a fair test of
// whether the update can be had at all. Pressing the button again was the
// player performing the retry by hand; the code does it now.
#define ATTEMPT_MAX 4

// What the worker was asked to do. The thread body switches on this once.
typedef enum
{
	JOB_CHECK,
	JOB_INSTALL,
} updateJob;

static bool  s_available;      ///< Did updaterInit get its services up.
static u32*  s_socBuf;
static bool  s_socUp, s_amUp;

static Thread s_worker;
static bool   s_workerLive;    ///< A thread exists and has not been reaped.
static updateJob s_job;

// Written by the worker, read by the main thread every frame.
//
// The ordering rule that makes this safe without a mutex: the worker fills in
// s_msg, s_latest, s_assetUrl and s_progress *before* it moves s_state to the
// value that makes them meaningful. The main thread reads s_state first and
// only then touches the rest. A word-sized store on ARM11 will not tear, so the
// worst a badly timed frame can do is paint one frame of stale progress.
static volatile updateState s_state = UPDATE_IDLE;
static volatile int  s_progress = -1;
static char s_msg[192];
static char s_latest[32];
static char s_assetUrl[512];

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void setMessage(const char* text)
{
	snprintf(s_msg, sizeof(s_msg), "%s", text ? text : "");
}

// Whether a failed attempt is worth repeating. A transport error or a 5xx is
// the network having a bad moment. A 404 or a 403 is an answer, and asking the
// same question four times only makes the player wait longer to hear it.
static bool worthRetrying(CURLcode result, long status)
{
	if (result != CURLE_OK) return true;
	return status == 0 || status >= 500;
}

// Grows 2s, 4s, 6s. Long enough for a wifi association to come back on its own,
// short enough that the player does not decide it has hung.
static void waitBeforeRetry(int attemptsDone)
{
	svcSleepThread((s64)attemptsDone * 2000000000LL);
}

// Says what went wrong and that it is being tried again, rather than leaving the
// screen on the last hopeful message for six seconds.
static void setRetryMessage(const char* what, const char* why, int attemptsDone)
{
	char text[192];
	snprintf(text, sizeof(text), "%s failed (%s). Trying again - %d of %d...",
	         what, why ? why : "no reason given", attemptsDone + 1, ATTEMPT_MAX);
	setMessage(text);
}

// Splits "v1.2.3", "1.2.3", "1.2" or "1" into three numbers. Anything it cannot
// read stays zero, which makes a malformed tag compare as older than any real
// release rather than triggering a spurious update.
static void versionParse(const char* text, int out[3])
{
	out[0] = out[1] = out[2] = 0;
	if (!text) return;

	while (*text == 'v' || *text == 'V' || *text == ' ') text++;

	for (int i = 0; i < 3 && *text; i++)
	{
		char* end = NULL;
		long value = strtol(text, &end, 10);
		if (end == text) break;

		out[i] = (int)value;
		text = end;
		if (*text != '.') break;
		text++;
	}
}

static bool versionIsNewer(const char* candidate, const char* current)
{
	int a[3], b[3];
	versionParse(candidate, a);
	versionParse(current, b);

	for (int i = 0; i < 3; i++)
	{
		if (a[i] != b[i]) return a[i] > b[i];
	}
	return false;
}

// Copies the JSON string value that starts at *cursor* - which must point at
// the opening quote - into out, undoing the two escapes GitHub actually emits
// in these fields. Returns the character after the closing quote, or NULL.
static const char* jsonCopyString(const char* cursor, char* out, size_t outSize)
{
	if (!cursor || *cursor != '"' || outSize == 0) return NULL;
	cursor++;

	size_t written = 0;
	while (*cursor && *cursor != '"')
	{
		char c = *cursor++;
		if (c == '\\' && *cursor)
		{
			char escaped = *cursor++;
			c = (escaped == 'n') ? '\n' : (escaped == 't') ? '\t' : escaped;
		}
		if (written + 1 < outSize) out[written++] = c;
	}

	out[written] = '\0';
	return (*cursor == '"') ? cursor + 1 : NULL;
}

// Finds "key": "value" and copies the value out. Deliberately naive - it does
// not understand nesting - which is fine because the two fields wanted here are
// unambiguous in a releases payload.
static const char* jsonFindValue(const char* json, const char* key)
{
	char pattern[64];
	snprintf(pattern, sizeof(pattern), "\"%s\"", key);

	const char* at = strstr(json, pattern);
	if (!at) return NULL;

	at += strlen(pattern);
	while (*at == ' ' || *at == ':' || *at == '\t' || *at == '\n' || *at == '\r') at++;
	return (*at == '"') ? at : NULL;
}

static bool jsonGetString(const char* json, const char* key, char* out, size_t outSize)
{
	const char* at = jsonFindValue(json, key);
	return at && jsonCopyString(at, out, outSize) != NULL;
}

// Walks every browser_download_url in the payload and keeps the first that ends
// in ".cia". A release can legitimately carry a .3dsx and a source zip too, and
// picking whichever came first would install the wrong thing.
static bool jsonFindCiaAsset(const char* json, char* out, size_t outSize)
{
	static const char* const key = "\"browser_download_url\"";
	const char* scan = json;

	while ((scan = strstr(scan, key)) != NULL)
	{
		scan += strlen(key);
		while (*scan == ' ' || *scan == ':') scan++;

		char candidate[512];
		const char* after = jsonCopyString(scan, candidate, sizeof(candidate));
		if (!after) break;
		scan = after;

		size_t length = strlen(candidate);
		if (length > 4 && strcmp(candidate + length - 4, ".cia") == 0)
		{
			snprintf(out, outSize, "%s", candidate);
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// curl plumbing
// ---------------------------------------------------------------------------

typedef struct
{
	char*  data;
	size_t length;
} memoryBuffer;

static size_t writeToMemory(char* chunk, size_t size, size_t count, void* userData)
{
	memoryBuffer* buffer = (memoryBuffer*)userData;
	size_t incoming = size * count;

	if (buffer->length + incoming > API_RESPONSE_MAX) return 0;

	char* grown = realloc(buffer->data, buffer->length + incoming + 1);
	if (!grown) return 0;

	buffer->data = grown;
	memcpy(buffer->data + buffer->length, chunk, incoming);
	buffer->length += incoming;
	buffer->data[buffer->length] = '\0';
	return incoming;
}

// The far end of the download: bytes go straight into the AM install handle
// rather than to a file on the SD card. Nothing is staged, so a 200KB update
// needs no free space and there is no half-written .cia to clean up.
typedef struct
{
	Handle handle;
	u64    offset;
	bool   failed;
} installSink;

static size_t writeToInstall(char* chunk, size_t size, size_t count, void* userData)
{
	installSink* sink = (installSink*)userData;
	size_t incoming = size * count;
	size_t done = 0;

	// FSFILE_Write is allowed to take fewer bytes than it was handed, and the AM
	// install handle does exactly that when the SD card is busy. curl treats any
	// return other than the full chunk as "the application refused this data" and
	// aborts the transfer - which is a download that was going fine dying part
	// way through for no reason the player can see. So keep pushing until the
	// chunk is gone, and only then agree that it was accepted.
	while (done < incoming)
	{
		u32 written = 0;
		if (R_FAILED(FSFILE_Write(sink->handle, &written, sink->offset,
		                          chunk + done, (u32)(incoming - done), 0)))
		{
			sink->failed = true;
			return done;
		}

		// No error and no progress. Returning here rather than spinning: the
		// alternative is a worker thread that never comes back.
		if (written == 0)
		{
			sink->failed = true;
			return done;
		}

		sink->offset += written;
		done += written;
	}

	return incoming;
}

static int reportProgress(void* userData, curl_off_t total, curl_off_t now,
                          curl_off_t upTotal, curl_off_t upNow)
{
	(void)userData; (void)upTotal; (void)upNow;

	if (total > 0)
	{
		s_progress = (int)((now * 100) / total);
	}
	return 0;
}

// Everything both requests need set the same way. Split out so the API call and
// the download cannot drift apart on the settings that matter - particularly
// the certificate bundle and redirect following, either of which silently
// breaks the updater if only one request has it.
static void applyCommonOptions(CURL* curl, const char* url)
{
	curl_easy_setopt(curl, CURLOPT_URL, url);

	// GitHub serves release assets as a redirect to a separate host, and it has
	// used relative Location headers in the past. Without this the download is a
	// zero-byte file that installs as a corrupt title.
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);

	// The API rejects requests with no User-Agent outright.
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "verdantpass-3ds/" VP_VERSION);

	// The whole reason RomFs exists in this project.
	curl_easy_setopt(curl, CURLOPT_CAINFO, "romfs:/cacert.pem");
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

	// A handheld on hotel wifi should give up and say so, not hang the front end
	// until the battery goes. These are per attempt, and there are ATTEMPT_MAX of
	// them, so they are deliberately shorter than they would be on their own -
	// giving up on a dead connection quickly and trying again beats waiting.
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 64L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

	// libcurl uses signals for some of its own timeouts unless told not to, and
	// all of this runs on a worker thread. Left on, the failure mode is not an
	// error - it is a transfer that never returns at all.
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

	// The default read buffer is 16KB, which is four times as many trips through
	// the console's socket service for the same three megabytes.
	curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 64L * 1024L);

	// A stalled connection on a handheld is usually a sleeping access point
	// rather than a dead one; probing keeps it from being dropped silently.
	curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
}

// ---------------------------------------------------------------------------
// The two jobs
// ---------------------------------------------------------------------------

// Pulls the version tag out of the URL GitHub redirects /releases/latest to.
// That URL is .../releases/tag/v1.1.1, so the tag is simply what follows.
static bool tagFromRedirect(const char* url, char* out, size_t outSize)
{
	static const char* const mark = "/releases/tag/";
	if (!url) return false;

	const char* found = strstr(url, mark);
	if (!found) return false;

	found += strlen(mark);
	if (*found == '\0') return false;

	snprintf(out, outSize, "%s", found);
	return true;
}

// Asks which release is current WITHOUT touching the JSON API.
//
// This is the fix for "it said it could not update, I tried again and it
// worked". api.github.com allows sixty unauthenticated requests an hour per IP
// address and answers 403 to the sixty-first - measured, not assumed:
//
//   HTTP/1.1 403 rate limit exceeded
//   X-RateLimit-Limit: 60
//   X-RateLimit-Remaining: 0
//
// Nothing was wrong with the console or the wifi. The address had simply spent
// its allowance, which anything else on the same connection can spend too, and
// an hour later it came back on its own. Retrying cannot help with that, and
// four retries would only spend four more of an allowance already gone.
//
// The plain release page has no such ceiling. /releases/latest answers 302 with
// the tag in the Location header, which is the only thing the check needs, so
// it is asked first and the API is kept as the fallback.
//
// lastResult comes back as the transport error of the final attempt, so the
// caller can tell "GitHub answered, just not with a redirect" from "nothing
// answered at all" - only the first of those is worth asking the API about.
static bool checkViaRedirect(char* tag, size_t tagSize, CURLcode* lastResult)
{
	char url[256];
	snprintf(url, sizeof(url), "https://github.com/%s/%s/releases/latest",
	         VP_REPO_OWNER, VP_REPO_NAME);

	for (int attempt = 0; attempt < ATTEMPT_MAX; attempt++)
	{
		CURL* curl = curl_easy_init();
		if (!curl) return false;

		applyCommonOptions(curl, url);

		// The redirect IS the answer here, so it must not be followed, and the
		// page body it would lead to is of no interest.
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);

		CURLcode result = curl_easy_perform(curl);
		long  status    = 0;
		char* redirect  = NULL;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redirect);
		*lastResult = result;

		// Read before cleanup - redirect points into the handle's own memory.
		bool got = (result == CURLE_OK && status >= 300 && status < 400 &&
		            tagFromRedirect(redirect, tag, tagSize));

		curl_easy_cleanup(curl);

		if (got) return true;
		if (!worthRetrying(result, status)) return false;
		if (attempt + 1 >= ATTEMPT_MAX) return false;

		setRetryMessage("The check",
		                result != CURLE_OK ? curl_easy_strerror(result) : "GitHub was busy",
		                attempt + 1);
		waitBeforeRetry(attempt + 1);
		setMessage("Asking GitHub for the latest release...");
	}

	return false;
}

// Builds the download URL from the tag rather than from a fixed filename,
// because every release names its CIA after its own version.
//
// The tag form is used in preference to /releases/latest/download/ so that a
// release published between the check and the download cannot quietly hand over
// a different version than the one the player was shown.
static void buildAssetUrl(const char* tag, char* out, size_t outSize)
{
	const char* number = (tag[0] == 'v' || tag[0] == 'V') ? tag + 1 : tag;
	snprintf(out, outSize, "https://github.com/%s/%s/releases/download/%s/%s%s%s",
	         VP_REPO_OWNER, VP_REPO_NAME, tag, VP_CIA_PREFIX, number, VP_CIA_SUFFIX);
}

// The JSON API route, kept as the fallback for the case where the release page
// stops redirecting the way it does today. Says why it failed and sets the
// failed state itself, so the caller has nothing to add.
static bool checkViaApi(char* tag, size_t tagSize, char* asset, size_t assetSize)
{
	char url[256];
	snprintf(url, sizeof(url),
	         "https://api.github.com/repos/%s/%s/releases/latest",
	         VP_REPO_OWNER, VP_REPO_NAME);

	memoryBuffer buffer = { NULL, 0 };
	CURLcode result = CURLE_OK;
	long status = 0;

	for (int attempt = 0; attempt < ATTEMPT_MAX; attempt++)
	{
		// Each attempt starts from an empty buffer. A half-received body from a
		// dropped connection followed by a whole one would parse as neither.
		free(buffer.data);
		buffer.data = NULL;
		buffer.length = 0;

		CURL* curl = curl_easy_init();
		if (!curl)
		{
			setMessage("Could not start the network client.");
			s_state = UPDATE_FAILED;
			return false;
		}

		applyCommonOptions(curl, url);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToMemory);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

		struct curl_slist* headers = curl_slist_append(NULL, "Accept: application/vnd.github+json");
		if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		result = curl_easy_perform(curl);
		status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

		if (headers) curl_slist_free_all(headers);
		curl_easy_cleanup(curl);

		if (!worthRetrying(result, status)) break;
		if (attempt + 1 >= ATTEMPT_MAX) break;

		setRetryMessage("The check",
		                result != CURLE_OK ? curl_easy_strerror(result) : "GitHub was busy",
		                attempt + 1);
		waitBeforeRetry(attempt + 1);
		setMessage("Asking GitHub for the latest release...");
	}

	if (result != CURLE_OK)
	{
		char reason[192];
		snprintf(reason, sizeof(reason), "Could not reach GitHub after %d tries: %s",
		         ATTEMPT_MAX, curl_easy_strerror(result));
		setMessage(reason);
		s_state = UPDATE_FAILED;
		free(buffer.data);
		return false;
	}

	if (status == 404)
	{
		// The overwhelmingly likely cause, and worth naming precisely: the repo
		// is reachable but has never had a release published, so there is
		// nothing to compare against.
		setMessage("No releases have been published for this add-on yet.");
		s_state = UPDATE_FAILED;
		free(buffer.data);
		return false;
	}

	if (status == 403)
	{
		// Named rather than shown as a bare number, because "403" tells a player
		// nothing and the thing it actually means - the allowance this internet
		// connection gets, not anything the console did - comes back by itself.
		setMessage("GitHub is limiting requests from this connection. "
		           "It clears by itself, usually within the hour.");
		s_state = UPDATE_FAILED;
		free(buffer.data);
		return false;
	}

	if (status != 200 || !buffer.data)
	{
		char reason[128];
		snprintf(reason, sizeof(reason), "GitHub answered with HTTP %ld.", status);
		setMessage(reason);
		s_state = UPDATE_FAILED;
		free(buffer.data);
		return false;
	}

	if (!jsonGetString(buffer.data, "tag_name", tag, tagSize))
	{
		setMessage("GitHub's answer did not contain a version tag.");
		s_state = UPDATE_FAILED;
		free(buffer.data);
		return false;
	}

	// The URL GitHub itself gave us, when it gave one. The caller builds the URL
	// from the tag if this comes back empty.
	if (!jsonFindCiaAsset(buffer.data, asset, assetSize)) asset[0] = '\0';

	free(buffer.data);
	return true;
}

static void runCheck(void)
{
	s_progress = -1;

	// Nothing to compare against. Said plainly and stopped here, because the
	// alternative - reading a blank version as 0.0.0 - would make every release
	// on GitHub look newer and offer an update that is not one.
	if (!VP_VERSION_SET)
	{
		setMessage("This build has no version number set, so there is nothing to compare.");
		s_state = UPDATE_FAILED;
		return;
	}

	setMessage("Asking GitHub for the latest release...");
	s_state = UPDATE_CHECKING;

	char tag[32]    = { 0 };
	char asset[512] = { 0 };

	// The redirect first, because it has no hourly allowance to run out of; the
	// API only if that route answered with something other than a redirect.
	CURLcode reached = CURLE_OK;
	if (!checkViaRedirect(tag, sizeof(tag), &reached))
	{
		// Nothing came back at all. The API is on the same internet, so asking
		// it would spend another four attempts arriving at the same sentence.
		if (reached != CURLE_OK)
		{
			char reason[192];
			snprintf(reason, sizeof(reason), "Could not reach GitHub after %d tries: %s",
			         ATTEMPT_MAX, curl_easy_strerror(reached));
			setMessage(reason);
			s_state = UPDATE_FAILED;
			return;
		}

		// checkViaApi has already put its own reason on screen.
		if (!checkViaApi(tag, sizeof(tag), asset, sizeof(asset))) return;
	}

	if (!versionIsNewer(tag, VP_VERSION))
	{
		snprintf(s_latest, sizeof(s_latest), "%s", tag);
		setMessage("This is the newest version.");
		s_state = UPDATE_UP_TO_DATE;
		return;
	}

	if (asset[0] == '\0') buildAssetUrl(tag, asset, sizeof(asset));
	snprintf(s_assetUrl, sizeof(s_assetUrl), "%s", asset);

	snprintf(s_latest, sizeof(s_latest), "%s", tag);
	setMessage("A newer version is available.");
	s_state = UPDATE_AVAILABLE;
}

static void runInstall(void)
{
	s_progress = 0;
	setMessage("Downloading the update...");
	s_state = UPDATE_DOWNLOADING;

	installSink sink = { 0, 0, false };
	if (R_FAILED(AM_StartCiaInstall(MEDIATYPE_SD, &sink.handle)))
	{
		setMessage("The console refused to start the install.");
		s_state = UPDATE_FAILED;
		return;
	}

	CURLcode result = CURLE_OK;
	long status = 0;
	bool got = false;

	for (int attempt = 0; attempt < ATTEMPT_MAX; attempt++)
	{
		if (attempt > 0)
		{
			// Start the title over from nothing rather than resuming into a
			// half-filled install handle. Resuming would need the far end to
			// honour a byte range, and there is no way to learn whether it did
			// until its bytes have already been written into the handle at the
			// wrong offsets. Three megabytes is cheap; a corrupt title is not.
			AM_CancelCIAInstall(sink.handle);
			if (R_FAILED(AM_StartCiaInstall(MEDIATYPE_SD, &sink.handle)))
			{
				setMessage("The console refused to start the install.");
				s_state = UPDATE_FAILED;
				return;
			}
			sink.offset = 0;
			sink.failed = false;
			s_progress = 0;
		}

		CURL* curl = curl_easy_init();
		if (!curl)
		{
			AM_CancelCIAInstall(sink.handle);
			setMessage("Could not start the network client.");
			s_state = UPDATE_FAILED;
			return;
		}

		applyCommonOptions(curl, s_assetUrl);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToInstall);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, reportProgress);

		result = curl_easy_perform(curl);
		status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		curl_easy_cleanup(curl);

		got = (result == CURLE_OK && status == 200 && !sink.failed && sink.offset > 0);
		if (got) break;

		// A 403 or a 404 on the asset is an answer, not a bad moment - the only
		// exception being the console refusing the write, which a fresh install
		// handle may well accept.
		if (!sink.failed && !worthRetrying(result, status)) break;
		if (attempt + 1 >= ATTEMPT_MAX) break;

		const char* why = sink.failed        ? "the console stopped accepting it"
		                : result != CURLE_OK ? curl_easy_strerror(result)
		                                     : "the download was refused";
		setRetryMessage("The download", why, attempt + 1);
		waitBeforeRetry(attempt + 1);
		setMessage("Downloading the update...");
	}

	// Failing here has to cancel the install handle. A handle left open is a
	// half-written title on the SD card that the player then has to delete by
	// hand through Data Management.
	if (!got)
	{
		AM_CancelCIAInstall(sink.handle);

		char reason[192];
		if (sink.failed)
			snprintf(reason, sizeof(reason),
			         "The console stopped accepting the download, %d tries running.",
			         ATTEMPT_MAX);
		else if (result != CURLE_OK)
			snprintf(reason, sizeof(reason), "Download failed after %d tries: %s",
			         ATTEMPT_MAX, curl_easy_strerror(result));
		else
			snprintf(reason, sizeof(reason), "Download failed with HTTP %ld.", status);

		setMessage(reason);
		s_state = UPDATE_FAILED;
		return;
	}

	s_progress = 100;
	setMessage("Installing...");
	s_state = UPDATE_INSTALLING;

	if (R_FAILED(AM_FinishCiaInstall(sink.handle)))
	{
		// Nothing to cancel here - Finish consumes the handle either way. The
		// usual cause is an unsigned title on a console without signature
		// patches, which is worth saying plainly rather than as an error code.
		setMessage("The install was rejected. Custom firmware with signature patches is required.");
		s_state = UPDATE_FAILED;
		return;
	}

	setMessage("Update installed.");
	s_state = UPDATE_DONE;
}

static void workerBody(void* argument)
{
	(void)argument;

	if (s_job == JOB_CHECK) runCheck();
	else                    runInstall();
}

// Reaps a finished worker so the next press can start a fresh one. Called from
// the main thread only, and only when the state says the worker has stopped.
static void reapWorker(void)
{
	if (!s_workerLive) return;

	threadJoin(s_worker, U64_MAX);
	threadFree(s_worker);
	s_workerLive = false;
}

static void startWorker(updateJob job)
{
	reapWorker();

	s_job = job;

	s32 priority = 0x30;
	svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);

	// One notch above the main thread. The worker spends nearly all its life
	// blocked on the network, so it costs the front end nothing, and it means a
	// busy frame cannot stall the download.
	priority -= 1;
	if (priority < 0x18) priority = 0x18;
	if (priority > 0x3F) priority = 0x3F;

	s_worker = threadCreate(workerBody, NULL, WORKER_STACK_SIZE, priority, -2, false);
	if (!s_worker)
	{
		setMessage("Could not start the update thread.");
		s_state = UPDATE_FAILED;
		return;
	}
	s_workerLive = true;
}

// ---------------------------------------------------------------------------
// Public surface
// ---------------------------------------------------------------------------

bool updaterInit(void)
{
	// RomFs carries the certificate bundle, but this app mounts it in main
	// before anything else because the track payload lives there too. The
	// original owned the mount here; two owners calling romfsInit/romfsExit
	// around each other is how the payload ends up unreadable mid-install.
	s_socBuf = (u32*)memalign(0x1000, SOC_BUFFER_SIZE);
	if (!s_socBuf) return false;

	if (R_FAILED(socInit(s_socBuf, SOC_BUFFER_SIZE)))
	{
		free(s_socBuf);
		s_socBuf = NULL;
		return false;
	}
	s_socUp = true;

	// am:net is in the RSF's service list, so this is expected to succeed on a
	// CIA build. It will not on a bare 3DSX without elevated services, which is
	// exactly the case the greyed-out button exists for.
	if (R_FAILED(amInit()))
	{
		socExit();
		s_socUp = false;
		free(s_socBuf);
		s_socBuf = NULL;
		return false;
	}
	s_amUp = true;

	curl_global_init(CURL_GLOBAL_DEFAULT);

	s_available = true;
	s_state = UPDATE_IDLE;
	s_latest[0] = '\0';
	s_assetUrl[0] = '\0';
	setMessage("");
	return true;
}

void updaterExit(void)
{
	reapWorker();

	if (s_available) curl_global_cleanup();

	if (s_amUp)    { amExit();    s_amUp = false; }
	if (s_socUp)   { socExit();   s_socUp = false; }
	if (s_socBuf)  { free(s_socBuf); s_socBuf = NULL; }
	// RomFs is not unmounted here: main mounts it and main unmounts it.

	s_available = false;
}

bool updaterAvailable(void)
{
	return s_available;
}

void updaterStartCheck(void)
{
	if (!s_available) return;

	updateState now = s_state;
	if (now == UPDATE_CHECKING || now == UPDATE_DOWNLOADING ||
	    now == UPDATE_INSTALLING || now == UPDATE_DONE) return;

	startWorker(JOB_CHECK);
}

void updaterStartInstall(void)
{
	if (!s_available || s_state != UPDATE_AVAILABLE) return;

	startWorker(JOB_INSTALL);
}

updateState updaterState(void)
{
	return s_state;
}

bool updaterBusy(void)
{
	updateState now = s_state;
	return now == UPDATE_CHECKING || now == UPDATE_DOWNLOADING || now == UPDATE_INSTALLING;
}

const char* updaterMessage(void)
{
	return s_msg;
}

int updaterProgress(void)
{
	return s_progress;
}

const char* updaterLatestVersion(void)
{
	return s_latest;
}

void updaterRelaunch(void)
{
	if (s_state != UPDATE_DONE) return;

	// Sets the target and returns; the jump itself happens when the app exits,
	// so the caller has to fall out of its main loop straight after this. By
	// then the title on the SD card is the new build, which is what comes back.
	aptSetChainloaderToSelf();
}
