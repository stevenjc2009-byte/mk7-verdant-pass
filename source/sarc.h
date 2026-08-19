// SARC, the archive inside a .szs.
//
// Every entry in every MK7 archive this project has looked at is *nameless*:
// there is no name table, and an entry is identified only by a hash stored in
// the SFAT node. So this reader keys on the hash and never touches names -
// which is also why patching is possible without knowing what Nintendo called
// anything.
//
// The layout below is from mk7-mod/tools/sarc.py, whose docstring records that
// every field was measured off real MK7 archives and that
// write_sarc(read_sarc(buf)) == buf was checked byte-for-byte on all of them.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "szs.h"

#define VP_SARC_MAX_ENTRIES 64

typedef struct {
    uint32_t hash;
    const unsigned char *data;   // points into the buffer passed to vp_sarc_read
    size_t size;
} vp_sarc_entry;

typedef struct {
    vp_sarc_entry entries[VP_SARC_MAX_ENTRIES];
    int count;
} vp_sarc;

// Parses a decompressed SARC. Borrows `buf`, which must outlive the vp_sarc.
// Refuses anything with a name table rather than silently dropping the names.
bool vp_sarc_read(const unsigned char *buf, size_t size, vp_sarc *out);

// Replaces the data of the entry with this hash. False if there is no such
// entry - which is the signal that the player's game is not the one this was
// built against, and is worth saying out loud rather than patching blind.
bool vp_sarc_replace(vp_sarc *arc, uint32_t hash,
                     const unsigned char *data, size_t size);

const vp_sarc_entry *vp_sarc_find(const vp_sarc *arc, uint32_t hash);

// Rebuilds the archive. Allocates *out; caller frees.
//
// Entries keep the order they were read in, which for a valid archive is hash
// ascending - MK7 binary-searches the SFAT by hash, so that order is load
// bearing, not cosmetic.
bool vp_sarc_write(const vp_sarc *arc, vp_buf *out);
