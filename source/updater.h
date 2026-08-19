// Checking GitHub for a newer Verdant Pass and installing it.
//
// Adapted from the Model Kit updater, which is the working version of this on
// this machine. Two things about it are worth knowing before reading updater.c.
//
// First, the console cannot verify github.com on its own. Its root certificate
// store predates every CA in use today, so a plain HTTPS request fails with a
// TLS error that no amount of retrying will fix. The fix is romfs:/cacert.pem,
// the Mozilla bundle, handed to libcurl explicitly.
//
// Second, the download runs on its own thread. 3DS wifi is slow enough that a
// synchronous fetch would lock the front end for the better part of a minute,
// which reads as a crash. The main loop polls updaterState() and paints; the
// worker does the waiting.
//
// What "an update" means here is worth stating plainly, because this app is an
// installer rather than a game: the track files ship inside this CIA's RomFS,
// so a new version of the track IS a new version of this app. The updater
// fetches and installs the newer CIA and relaunches into it; that build then
// writes the newer track to the SD card. There is no separate track-only
// download to keep in step.

#pragma once

#include <3ds.h>
#include <stdbool.h>

#include "version.h"

// Where to ask. Kept here rather than buried in the .c so that forking the
// repo is a one-line change.
#define VP_REPO_OWNER "stevenjc2009-byte"
#define VP_REPO_NAME  "mk7-verdant-pass"

// Releases name their asset with the version on the end - verdantpass1.0.0.cia
// and so on - so there is no single filename to ask for. The updater does not
// need one: it reads the real download URL out of the API response and takes
// whatever .cia the release actually carries.
//
// This is only the last-resort fallback, used if the API answers with something
// that cannot be parsed. It resolves only if a release also carries a copy under
// this fixed name, which is the same copy the install QR code points at.
#define VP_CIA_ASSET  "verdantpass.cia"

// Where the updater has got to. The front end draws from this and nothing else.
typedef enum
{
    UPDATE_IDLE,         ///< Nothing has been asked for yet.
    UPDATE_CHECKING,     ///< Talking to GitHub.
    UPDATE_UP_TO_DATE,   ///< Asked, and this build is already the newest.
    UPDATE_AVAILABLE,    ///< A newer release exists; waiting on the player.
    UPDATE_DOWNLOADING,  ///< Streaming the .cia into the install handle.
    UPDATE_INSTALLING,   ///< Download finished, AM is committing the title.
    UPDATE_DONE,         ///< Installed. The app should relaunch itself now.
    UPDATE_FAILED,       ///< Gave up. updaterMessage() says why.
} updateState;

// Brings up the services the updater needs - sockets and AM.
//
// Unlike the Model Kit original this does NOT mount RomFs: this app reads its
// track payload out of RomFs too, so main owns the mount and brings it up
// before anything else. Two owners calling romfsInit/romfsExit around each
// other is how the payload ends up unreadable half way through an install.
//
// Returns false if a service refused, in which case the update button reports
// itself unavailable rather than the app failing to boot.
bool updaterInit(void);
void updaterExit(void);

// False when updaterInit could not get its services up - which is the normal
// case for a .3dsx build, where AM is not available.
bool updaterAvailable(void);

// Starts the version check on the worker thread and returns immediately.
// Ignored unless the updater is idle or settled from a previous attempt.
void updaterStartCheck(void);

// Starts the download and install, also on the worker thread. Only meaningful
// once a check has reported UPDATE_AVAILABLE; ignored otherwise.
void updaterStartInstall(void);

// Where things are. Cheap; call every frame.
updateState updaterState(void);

// True while the worker thread owns the job and the player must not be offered
// a way out - a cancelled download would leave a half-written title behind.
bool updaterBusy(void);

// One line of plain English - the failure reason when the state is
// UPDATE_FAILED, a description of what is happening otherwise.
const char* updaterMessage(void);

// Percent of the download completed, or -1 when that is not a meaningful
// question yet.
int updaterProgress(void);

// The newest release tag GitHub reported, valid from UPDATE_AVAILABLE onward.
const char* updaterLatestVersion(void);

// Points the chainloader back at this title, which after a successful install
// is the new build. Only meaningful on UPDATE_DONE.
//
// It returns immediately - the jump happens when the app exits - so the caller
// must fall out of its main loop right after calling this.
void updaterRelaunch(void);
