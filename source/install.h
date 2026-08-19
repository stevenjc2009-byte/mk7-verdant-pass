// Writing the Verdant Pass track onto the SD card in the layout Hotswap reads.
//
// Hotswap's format is deliberately plain, which is what makes this app small:
// a mod is a directory under /hotswap/<game>/, its files live in a `layeredfs`
// subfolder mirroring the game's romfs, and activating it *renames* that folder
// into /luma/titles/<TITLEID>/. Nothing is copied at swap time and there is no
// manifest to register with -- the folder name is the label Hotswap shows.
//
// So installing is exactly: create the folder, write the files into it, done.
// The mod appears in Hotswap's list the next time it scans.

#pragma once

#include <stdbool.h>
#include <stddef.h>

// The name of the folder, and therefore the name the player sees in Hotswap's
// mod list. Hotswap uses the directory name as the display text directly, so
// this is spelled the way it should read on screen.
#define VP_MOD_SLUG "Verdant Pass"

// Hotswap's own slug for Mario Kart 7, from its games.c registry. This is the
// folder under /hotswap/, and it is NOT a title ID.
#define VP_GAME_SLUG "mk7"

// Where the payload sits inside this app's own RomFS.
//
// Overridable so the host test harness can point the same code at a temporary
// directory. The console build never defines it and gets the RomFS path.
#ifndef VP_PAYLOAD_ROOT
#define VP_PAYLOAD_ROOT "romfs:/mod"
#endif

typedef enum {
    VP_OK = 0,
    VP_ERR_ACTIVE,      // the mod is currently swapped in; Hotswap owns the files
    VP_ERR_MKDIR,       // a destination directory could not be created
    VP_ERR_READ,        // the payload could not be read out of RomFS
    VP_ERR_WRITE,       // a file could not be written to the SD card
    VP_ERR_VERIFY       // a file was written but read back the wrong size
} vp_result_t;

// Progress, called once per file as it starts. `name` is the path relative to
// the payload root. Safe to be NULL.
typedef void (*vp_progress_fn)(const char *name, int done, int total, void *user);

// How many files and bytes the payload holds. Counted by walking RomFS rather
// than hardcoded, so adding a file to the payload needs no code change here.
bool vp_payload_stat(int *files, unsigned long long *bytes);

// True if Hotswap currently has this mod swapped into the LayeredFS slot.
//
// When it does, the files are not at the parked path at all -- Hotswap has
// renamed the whole `layeredfs` folder into /luma/titles/<TITLEID>/ -- and
// writing a fresh one underneath it would leave two copies and a state file
// that disagrees with the disk. Detected by reading `layeredfs=` out of
// /hotswap/mk7/state, which needs no knowledge of which region's title is
// installed.
bool vp_mod_is_active(void);

// Does the parked copy already exist on the card.
bool vp_is_installed(void);

// Writes the payload to sdmc:/hotswap/mk7/<VP_MOD_SLUG>/layeredfs/romfs/,
// creating every directory on the way and overwriting whatever is there.
//
// Every file is read back and its size checked before the install is called
// good: a short write on a failing SD card is otherwise completely silent, and
// the symptom would be MK7 crashing at track load with nothing to point at.
vp_result_t vp_install(vp_progress_fn progress, void *user,
                       int *files_written, unsigned long long *bytes_written);

// A sentence for the player, for any result.
const char *vp_result_str(vp_result_t r);

// The destination path, for showing on screen. Fills `out` and returns it.
const char *vp_dest_path(char *out, size_t cap);
