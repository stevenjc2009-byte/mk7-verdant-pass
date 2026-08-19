// Building the Verdant Pass track onto the SD card in the layout Hotswap reads.
//
// Hotswap's format is deliberately plain, which is what makes this app small:
// a mod is a directory under /hotswap/<game>/, its files live in a `layeredfs`
// subfolder mirroring the game's romfs, and activating it *renames* that folder
// into /luma/titles/<TITLEID>/. Nothing is copied at swap time and there is no
// manifest to register with -- the folder name is the label Hotswap shows.
//
// What goes into that folder is not shipped with this app. The nine files MK7
// needs are ninety-odd percent Nintendo's data, so instead the app carries only
// the three files this project generated - the model, the collision mesh and
// the course layout - reads the rest out of the player's own game, and does the
// assembly here on the console. See patch.h for the recipe and gamefs.h for how
// the game is opened.

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

// Where our three generated files sit inside this app's own RomFS.
//
// Overridable so the host test harness can point the same code at the build
// directory. The console build never defines it and gets the RomFS path.
#ifndef VP_PAYLOAD_ROOT
#define VP_PAYLOAD_ROOT "romfs:/mod"
#endif

typedef enum {
    VP_OK = 0,
    VP_ERR_ACTIVE,      // the mod is currently swapped in; Hotswap owns the files
    VP_ERR_GAME,        // the player's Mario Kart 7 could not be opened
    VP_ERR_MKDIR,       // a destination directory could not be created
    VP_ERR_READ,        // our own payload could not be read out of RomFS
    VP_ERR_GAME_READ,   // a file could not be read out of the player's game
    VP_ERR_PATCH,       // the game's files are not what this track was built against
    VP_ERR_MEMORY,      // ran out of room assembling a file
    VP_ERR_WRITE,       // a file could not be written to the SD card
    VP_ERR_VERIFY       // a file was written but read back the wrong size
} vp_result_t;

// How many files the install writes: the course, plus one per language.
int vp_step_count(void);

// True if this build's three generated files are present and readable, and how
// big they are together (may be NULL).
//
// Checked at boot so a build with a broken or missing RomFS says so on its first
// screen, rather than looking healthy and then failing part way through an
// install with the destination folder already half written.
bool vp_payload_ok(unsigned long long *bytes);

// Progress, called once per file as it starts. `name` is the path relative to
// the game's romfs root, e.g. "Course/Gn64_KalimariDesert.szs". Safe to be NULL.
typedef void (*vp_progress_fn)(const char *name, int done, int total, void *user);

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

// Assembles the track from the player's game and writes it to
// sdmc:/hotswap/mk7/<VP_MOD_SLUG>/layeredfs/romfs/, creating every directory on
// the way and overwriting whatever is there.
//
// Every file is read back and its size checked before the install is called
// good: a short write on a failing SD card is otherwise completely silent, and
// the symptom would be MK7 crashing at track load with nothing to point at.
vp_result_t vp_install(vp_progress_fn progress, void *user,
                       int *files_written, unsigned long long *bytes_written);

// A sentence for the player, for any result. For the failures that can carry
// detail from further down -- a bad game file, a failed allocation -- this is
// the message from patch.h or gamefs.h rather than a generic one.
const char *vp_result_str(vp_result_t r);

// The destination path, for showing on screen. Fills `out` and returns it.
const char *vp_dest_path(char *out, size_t cap);
