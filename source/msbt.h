// MsgStdBn - the Nintendo message binary that holds every string the UI shows.
//
// This app changes exactly one of them: the course name, entry 129 of the TXT2
// section in the file whose SARC hash is 0x1DAA5659, inside UI/common-*.szs.
//
// TXT2 stores strings by offset rather than in fixed slots, so a replacement
// may be any length as long as the pointer table, the section size and the
// file-size field at 0x12 are all recomputed. That is what makes renaming a
// course possible at all, and it is why this is a rewrite rather than a poke.
//
// Ported from mk7-mod/tools/msbt.py, which re-derives every offset from the
// file's own headers so a wrong guess fails loudly instead of writing a
// plausible-looking corrupt file.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "szs.h"

// Replaces TXT2 entry `index` with `text`, which is ASCII and is stored as
// UTF-16LE. Allocates *out; caller frees.
//
// ASCII only, deliberately: the one string this app writes is "Verdant Pass".
// Accepting UTF-8 would mean carrying a decoder for a case that cannot arise,
// and a silently mis-encoded course name is worse than a compile-time limit.
bool vp_msbt_set_string(const unsigned char *buf, size_t size,
                        int index, const char *text, vp_buf *out);

// Reads one string back as ASCII, for verification. Any code point outside
// ASCII becomes '?'. Returns false if the file or the index is bad.
bool vp_msbt_get_string(const unsigned char *buf, size_t size,
                        int index, char *out, size_t cap);
