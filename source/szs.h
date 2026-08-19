// Yaz0, the compression Nintendo wraps around a SARC to make a .szs.
//
// This app needs both directions because it patches the player's own game
// files rather than shipping modified ones: read their .szs, take it apart,
// put ours back together, write a .szs the game will accept.
//
// The format is documented in mk7-mod/tools/yaz0.py, which was verified
// against real ROM files with 3dstool as independent ground truth. This is a
// straight port of that, so the two agree by construction and the host test
// checks that they agree in fact.

#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    unsigned char *data;
    size_t size;
} vp_buf;

void vp_buf_free(vp_buf *b);

// Decompresses a whole .szs file. Allocates *out; caller frees.
//
// Every length and offset in the stream is checked against the output buffer
// before it is used. A corrupt or truncated file returns false rather than
// walking off the end of the allocation - this runs on a console with no
// memory protection worth the name, and the input is a file on a card that
// might be failing.
bool vp_yaz0_decompress(const unsigned char *src, size_t src_size, vp_buf *out);

// Compresses into a .szs. Allocates *out; caller frees.
//
// LZ77 with a 4096-byte window and a depth-limited hash chain, which is the
// cheapest thing that still produces a file the same order of size as
// Nintendo's. Storing everything as literals would also decode correctly but
// would inflate the course archive from ~1.8 MB to ~3.4 MB.
//
// It does not have to match Nintendo's encoder bit for bit. It has to be
// something their decoder inverts exactly, and the host test proves that on
// the real archives.
bool vp_yaz0_compress(const unsigned char *src, size_t src_size, vp_buf *out);
