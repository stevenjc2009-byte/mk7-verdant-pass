#include "install.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "gamefs.h"
#include "patch.h"
#include "szs.h"

// The console's mkdir is the POSIX two-argument one; MinGW's is one argument.
// The host test harness compiles this exact file, so the difference is bridged
// here. It cannot be bridged from the harness with a macro: a `mkdir(p,m)`
// macro would also try to rewrite the one-argument declaration in the system
// header. Inert on the console, where _WIN32 is never defined.
#ifdef _WIN32
#include <direct.h>
#define vp_mkdir(path) _mkdir(path)
#else
#define vp_mkdir(path) mkdir((path), 0777)
#endif

// Overridable for the host test harness, same reason as VP_PAYLOAD_ROOT.
#ifndef SD_ROOT
#define SD_ROOT "sdmc:/"
#endif
#define HOTSWAP_ROOT SD_ROOT "hotswap"

// One size for every path buffer in this file. The longest real path is about
// 80 characters; the headroom is for a deeper tree later, and path_join below
// turns "it did not fit" into an error rather than a wrong filename.
#define VP_PATH_MAX 512

// Set by whichever step failed, so vp_result_str can hand the player the real
// reason rather than a category. Cleared at the top of every install.
static const char *s_detail = NULL;

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

// Joins two path parts and reports whether the result fit.
//
// The check is not there to satisfy -Wformat-truncation. A truncated path here
// would not be a cosmetic problem: it would name a *different* file, and the
// install would quietly write to the wrong place. Every caller treats a false
// return as a hard failure.
static bool path_join(char *out, size_t cap, const char *head, const char *tail)
{
    int n = snprintf(out, cap, "%s/%s", head, tail);
    return n > 0 && (size_t)n < cap;
}

static bool mod_dir(char *out, size_t cap)
{
    int n = snprintf(out, cap, "%s/%s/%s", HOTSWAP_ROOT, VP_GAME_SLUG, VP_MOD_SLUG);
    return n > 0 && (size_t)n < cap;
}

const char *vp_dest_path(char *out, size_t cap)
{
    char base[VP_PATH_MAX];
    if (!mod_dir(base, sizeof(base)) ||
        !path_join(out, cap, base, "layeredfs/romfs"))
    {
        if (cap) out[0] = '\0';
    }
    return out;
}

// mkdir -p. Walks the string creating each component, and treats "it already
// exists" as success -- which is the normal case for every component except the
// last on a reinstall.
static bool make_dirs(const char *path)
{
    char work[VP_PATH_MAX];
    snprintf(work, sizeof(work), "%s", path);

    // Skip the "sdmc:/" prefix: the drive itself is not a directory that can be
    // created, and asking for it fails.
    char *cursor = work;
    if (strncmp(cursor, SD_ROOT, strlen(SD_ROOT)) == 0) cursor += strlen(SD_ROOT);

    for (; *cursor; cursor++)
    {
        if (*cursor != '/') continue;

        *cursor = '\0';
        if (vp_mkdir(work) != 0 && errno != EEXIST) return false;
        *cursor = '/';
    }

    return vp_mkdir(work) == 0 || errno == EEXIST;
}

// Creates the directory a file is about to be written into.
static bool make_parent_dirs(const char *file_path)
{
    char dir[VP_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", file_path);

    char *cut = strrchr(dir, '/');
    if (!cut) return true;

    *cut = '\0';
    return make_dirs(dir);
}

static bool path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

// ---------------------------------------------------------------------------
// Hotswap state
// ---------------------------------------------------------------------------

bool vp_mod_is_active(void)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s/state", HOTSWAP_ROOT, VP_GAME_SLUG);

    FILE *f = fopen(path, "rb");
    if (!f) return false;              // no state file: nothing has been swapped

    // Hotswap's state is three `key=value` lines and never grows, so a fixed
    // buffer that reads the whole thing is enough and avoids a line loop.
    char buf[256];
    size_t got = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[got] = '\0';

    const char *at = strstr(buf, "layeredfs=");
    if (!at) return false;             // pre-per-slot state file; nothing to match
    at += strlen("layeredfs=");

    size_t len = strcspn(at, "\r\n");
    return len == strlen(VP_MOD_SLUG) && strncmp(at, VP_MOD_SLUG, len) == 0;
}

bool vp_is_installed(void)
{
    char dest[VP_PATH_MAX];
    vp_dest_path(dest, sizeof(dest));
    return path_exists(dest);
}

int vp_step_count(void) { return 1 + VP_UI_FILE_COUNT; }

// The three files this project generated, and the only data this app ships.
static const char *const PAYLOAD_FILES[] = { "course.bcmdl", "course.kcl", "course.kmp" };
#define PAYLOAD_FILE_COUNT (int)(sizeof(PAYLOAD_FILES) / sizeof(PAYLOAD_FILES[0]))

bool vp_payload_ok(unsigned long long *bytes)
{
    unsigned long long total = 0;

    for (int i = 0; i < PAYLOAD_FILE_COUNT; i++)
    {
        char path[VP_PATH_MAX];
        struct stat st;
        if (!path_join(path, sizeof(path), VP_PAYLOAD_ROOT, PAYLOAD_FILES[i])) return false;
        if (stat(path, &st) != 0 || st.st_size <= 0) return false;
        total += (unsigned long long)st.st_size;
    }

    if (bytes) *bytes = total;
    return true;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

static bool read_whole(const char *path, vp_buf *out)
{
    out->data = NULL;
    out->size = 0;

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long n = ftell(f);
    if (n <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }

    unsigned char *data = (unsigned char *)malloc((size_t)n);
    if (!data) { fclose(f); return false; }

    size_t got = fread(data, 1, (size_t)n, f);
    fclose(f);

    if (got != (size_t)n) { free(data); return false; }

    out->data = data;
    out->size = got;
    return true;
}

// Writes a buffer and reads its size back.
//
// The read-back is not paranoia. A short write to a tired SD card returns
// success from fwrite and only shows up much later as MK7 hanging on the track
// load screen, with nothing on the console to say why. Checking here turns a
// silent bad install into a message.
static vp_result_t write_whole(const char *path, const vp_buf *buf)
{
    if (!make_parent_dirs(path)) return VP_ERR_MKDIR;

    FILE *f = fopen(path, "wb");
    if (!f) return VP_ERR_WRITE;

    bool ok = fwrite(buf->data, 1, buf->size, f) == buf->size;

    // fclose is where a full card actually reports itself, because the last
    // buffer is flushed here rather than by the fwrite above.
    if (fclose(f) != 0) ok = false;
    if (!ok) return VP_ERR_WRITE;

    struct stat st;
    if (stat(path, &st) != 0 || (unsigned long long)st.st_size != buf->size)
        return VP_ERR_VERIFY;

    return VP_OK;
}

// ---------------------------------------------------------------------------
// The install
// ---------------------------------------------------------------------------

typedef struct {
    const char *game_root;
    char dest_root[VP_PATH_MAX];
    vp_progress_fn progress;
    void *user;
    int done;
    int total;
    unsigned long long bytes;
} build_ctx_t;

// Reads one file out of the player's game.
static vp_result_t read_game_file(const build_ctx_t *ctx, const char *rel, vp_buf *out)
{
    char path[VP_PATH_MAX];
    if (!path_join(path, sizeof(path), ctx->game_root, rel)) return VP_ERR_GAME_READ;
    if (!read_whole(path, out))
    {
        s_detail = "A file is missing from this copy of Mario Kart 7.";
        return VP_ERR_GAME_READ;
    }
    return VP_OK;
}

// Writes one finished file into the parked mod folder.
static vp_result_t write_mod_file(build_ctx_t *ctx, const char *rel, const vp_buf *buf)
{
    char path[VP_PATH_MAX];
    if (!path_join(path, sizeof(path), ctx->dest_root, rel)) return VP_ERR_WRITE;

    vp_result_t r = write_whole(path, buf);
    if (r != VP_OK) return r;

    ctx->done++;
    ctx->bytes += buf->size;
    return VP_OK;
}

static vp_result_t build_course(build_ctx_t *ctx)
{
    if (ctx->progress) ctx->progress(VP_COURSE_FILE, ctx->done, ctx->total, ctx->user);

    // Our three generated files. Loaded here rather than at the top of the
    // install so they are freed again before the eight language files run --
    // they are two megabytes, and the console has no room to spare while the
    // course archive is being rebuilt and recompressed.
    vp_payload payload = { { NULL, 0 }, { NULL, 0 }, { NULL, 0 } };
    char path[VP_PATH_MAX];
    bool loaded =
        path_join(path, sizeof(path), VP_PAYLOAD_ROOT, "course.bcmdl") &&
        read_whole(path, &payload.cgfx) &&
        path_join(path, sizeof(path), VP_PAYLOAD_ROOT, "course.kcl") &&
        read_whole(path, &payload.kcl) &&
        path_join(path, sizeof(path), VP_PAYLOAD_ROOT, "course.kmp") &&
        read_whole(path, &payload.kmp);

    vp_buf stock = { NULL, 0 }, built = { NULL, 0 };
    vp_result_t r = VP_OK;

    if (!loaded)
    {
        s_detail = "The track data inside this app could not be read.";
        r = VP_ERR_READ;
    }
    else if ((r = read_game_file(ctx, VP_COURSE_FILE, &stock)) != VP_OK)
    {
        /* s_detail already set */
    }
    else if (!vp_patch_course(stock.data, stock.size, &payload, &built))
    {
        s_detail = vp_patch_error();
        r = VP_ERR_PATCH;
    }
    else
    {
        r = write_mod_file(ctx, VP_COURSE_FILE, &built);
    }

    vp_buf_free(&built);
    vp_buf_free(&stock);
    vp_buf_free(&payload.kmp);
    vp_buf_free(&payload.kcl);
    vp_buf_free(&payload.cgfx);
    return r;
}

// The eight language archives, each with one string changed: the course name.
// All of them, not just English -- the name is what the player reads on the cup
// screen, and a Dutch console showing "Kalimari Desert" would look like the
// install half worked.
static vp_result_t build_ui(build_ctx_t *ctx)
{
    for (int i = 0; i < VP_UI_FILE_COUNT; i++)
    {
        if (ctx->progress) ctx->progress(VP_UI_FILES[i], ctx->done, ctx->total, ctx->user);

        vp_buf stock = { NULL, 0 }, built = { NULL, 0 };
        vp_result_t r = read_game_file(ctx, VP_UI_FILES[i], &stock);

        if (r == VP_OK)
        {
            if (!vp_patch_ui(stock.data, stock.size, &built))
            {
                s_detail = vp_patch_error();
                r = VP_ERR_PATCH;
            }
            else
            {
                r = write_mod_file(ctx, VP_UI_FILES[i], &built);
            }
        }

        vp_buf_free(&built);
        vp_buf_free(&stock);
        if (r != VP_OK) return r;
    }
    return VP_OK;
}

vp_result_t vp_install(vp_progress_fn progress, void *user,
                       int *files_written, unsigned long long *bytes_written)
{
    if (files_written)  *files_written = 0;
    if (bytes_written)  *bytes_written = 0;
    s_detail = NULL;

    // Refuse rather than fight Hotswap for the files. While the mod is swapped
    // in, its `layeredfs` folder has been renamed away into /luma/titles/, and
    // writing a new one here would leave Hotswap's state pointing at one copy
    // and the player looking at another. Telling them to swap back to stock
    // first is one sentence and cannot corrupt anything.
    if (vp_mod_is_active()) return VP_ERR_ACTIVE;

    const char *game_root = vp_gamefs_open();
    if (!game_root)
    {
        s_detail = vp_gamefs_error();
        return VP_ERR_GAME;
    }

    build_ctx_t ctx = { game_root, { 0 }, progress, user, 0, vp_step_count(), 0 };
    vp_dest_path(ctx.dest_root, sizeof(ctx.dest_root));

    vp_result_t r = make_dirs(ctx.dest_root) ? VP_OK : VP_ERR_MKDIR;
    if (r == VP_OK) r = build_course(&ctx);
    if (r == VP_OK) r = build_ui(&ctx);

    vp_gamefs_close();

    if (files_written)  *files_written = ctx.done;
    if (bytes_written)  *bytes_written = ctx.bytes;
    return r;
}

const char *vp_result_str(vp_result_t r)
{
    // The failures that can carry a real reason from further down say it,
    // rather than a category the player can do nothing with.
    if (s_detail && (r == VP_ERR_GAME || r == VP_ERR_GAME_READ ||
                     r == VP_ERR_PATCH || r == VP_ERR_READ))
        return s_detail;

    switch (r)
    {
        case VP_OK:            return "Installed.";
        case VP_ERR_ACTIVE:    return "Verdant Pass is swapped in right now. Open Hotswap, "
                                      "switch Mario Kart 7 back to Stock, then run this again.";
        case VP_ERR_GAME:      return "Mario Kart 7 was not found on this console.";
        case VP_ERR_MKDIR:     return "Could not create the folder on the SD card.";
        case VP_ERR_READ:      return "Could not read the track out of this app.";
        case VP_ERR_GAME_READ: return "Could not read from your copy of Mario Kart 7.";
        case VP_ERR_PATCH:     return "This copy of Mario Kart 7 is not one this track "
                                      "was built for.";
        case VP_ERR_MEMORY:    return "Ran out of memory building the track.";
        case VP_ERR_WRITE:     return "Could not write to the SD card. It may be full or locked.";
        case VP_ERR_VERIFY:    return "A file was written but came back the wrong size. "
                                      "The SD card may be failing.";
    }
    return "Unknown error.";
}
