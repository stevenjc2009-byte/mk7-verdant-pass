// The recipe: turn the player's own Mario Kart 7 files into the modded ones.
//
// This is the whole reason the app exists in this shape. Shipping the finished
// .szs files would mean redistributing Nintendo's data - the course archive is
// fifteen entries and only three of them are ours, and the UI archives are
// entirely theirs with one string changed. So instead the app carries only the
// three files this project generated, reads the rest out of the game the
// player already owns, and does the assembly on the console.
//
// Nothing in here touches a 3DS API. That is on purpose: the same code runs in
// the host test against the reference ROM extract, and is checked against the
// output of the PC toolchain that built the track in the first place.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "szs.h"

// The three SARC entries this project generated, by hash. Nameless archive, so
// the hash is the only identifier there is.
#define VP_HASH_CGFX 0x3546EF6Fu    // the track model
#define VP_HASH_KCL  0x8887D847u    // the collision mesh
#define VP_HASH_KMP  0x8887DC3Du    // the course layout

// Where the course name lives: SARC entry 0x1DAA5659 inside UI/common-*.szs,
// TXT2 index 129 (message id 1129).
#define VP_HASH_MSBT   0x1DAA5659u
#define VP_NAME_INDEX  129
#define VP_COURSE_NAME "Verdant Pass"

// The stock file this replaces.
#define VP_COURSE_FILE "Course/Gn64_KalimariDesert.szs"

// The eight European language archives. The name is in all of them, and a
// player whose console language is Dutch should not see the English one.
extern const char *const VP_UI_FILES[];
extern const int VP_UI_FILE_COUNT;

typedef struct {
    vp_buf cgfx;
    vp_buf kcl;
    vp_buf kmp;
} vp_payload;

// Builds the modded course archive from the player's own .szs.
//
// Substitutes our three entries and leaves the other twelve exactly as they
// were, then rebuilds and recompresses. Fails rather than guessing if any of
// the three hashes is missing, which is what a different game version would
// look like.
bool vp_patch_course(const unsigned char *stock_szs, size_t stock_size,
                     const vp_payload *payload, vp_buf *out);

// Builds a modded UI archive with the course renamed.
bool vp_patch_ui(const unsigned char *stock_szs, size_t stock_size, vp_buf *out);

// Why the last patch call failed, for the screen. Never NULL.
const char *vp_patch_error(void);
