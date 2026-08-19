#include "szs.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define YAZ0_HEADER 16

// 12-bit distance field, so a back-reference reaches at most this far.
#define WINDOW      0x1000
#define MIN_MATCH   3
#define MAX_MATCH   (0xFF + 0x12)     // 273, the 3-byte encoding's ceiling
#define SHORT_MATCH 0x11              // 17, the 2-byte encoding's ceiling

// How many candidate positions to try per match. Deflate uses hundreds; this
// runs on a 268 MHz ARM11 against a 3 MB archive, and past about 16 the ratio
// stops moving while the time does not.
#define MAX_CHAIN   16

void vp_buf_free(vp_buf *b)
{
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->size = 0;
}

static uint32_t read_be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static void write_be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

// ---------------------------------------------------------------------------
// Decompress
// ---------------------------------------------------------------------------

bool vp_yaz0_decompress(const unsigned char *src, size_t src_size, vp_buf *out)
{
    out->data = NULL;
    out->size = 0;

    if (src_size < YAZ0_HEADER || memcmp(src, "Yaz0", 4) != 0) return false;

    size_t size = read_be32(src + 4);
    if (size == 0) return false;

    unsigned char *dst = (unsigned char *)malloc(size);
    if (!dst) return false;

    size_t in = YAZ0_HEADER, pos = 0;
    unsigned flags = 0;
    int bits_left = 0;

    while (pos < size)
    {
        if (bits_left == 0)
        {
            if (in >= src_size) { free(dst); return false; }
            flags = src[in++];
            bits_left = 8;
        }

        bool literal = (flags & 0x80) != 0;
        flags <<= 1;
        bits_left--;

        if (literal)
        {
            if (in >= src_size) { free(dst); return false; }
            dst[pos++] = src[in++];
            continue;
        }

        if (in + 1 >= src_size) { free(dst); return false; }
        unsigned b0 = src[in], b1 = src[in + 1];
        in += 2;

        size_t distance = (size_t)((b0 & 0x0F) << 8 | b1);
        size_t length = b0 >> 4;

        if (length == 0)
        {
            if (in >= src_size) { free(dst); return false; }
            length = (size_t)src[in++] + 0x12;
        }
        else
        {
            length += 2;
        }

        // Both of these are the difference between a bad file and a wild
        // write, so neither is an assert.
        if (distance + 1 > pos)          { free(dst); return false; }
        if (length > size - pos)         { free(dst); return false; }

        size_t from = pos - distance - 1;

        // Byte at a time, deliberately: source and destination overlap when a
        // run is longer than the distance, and that overlap is how the format
        // expresses runs at all. memcpy would be wrong here.
        for (size_t i = 0; i < length; i++) dst[pos + i] = dst[from + i];
        pos += length;
    }

    out->data = dst;
    out->size = size;
    return true;
}

// ---------------------------------------------------------------------------
// Compress
// ---------------------------------------------------------------------------

#define HASH_BITS 16
#define HASH_SIZE (1 << HASH_BITS)

static unsigned hash3(const unsigned char *p)
{
    return (unsigned)(((p[0] << 10) ^ (p[1] << 5) ^ p[2]) & (HASH_SIZE - 1));
}

typedef struct {
    int32_t *head;   // hash -> most recent position, -1 for none
    int32_t *prev;   // position & (WINDOW-1) -> previous position with that hash
} chain_t;

static bool chain_init(chain_t *c)
{
    c->head = (int32_t *)malloc(sizeof(int32_t) * HASH_SIZE);
    c->prev = (int32_t *)malloc(sizeof(int32_t) * WINDOW);
    if (!c->head || !c->prev)
    {
        free(c->head); free(c->prev);
        c->head = c->prev = NULL;
        return false;
    }
    memset(c->head, 0xff, sizeof(int32_t) * HASH_SIZE);   // -1
    memset(c->prev, 0xff, sizeof(int32_t) * WINDOW);
    return true;
}

static void chain_free(chain_t *c)
{
    free(c->head); free(c->prev);
    c->head = c->prev = NULL;
}

static void chain_insert(chain_t *c, const unsigned char *buf, size_t pos)
{
    unsigned h = hash3(buf + pos);
    c->prev[pos & (WINDOW - 1)] = c->head[h];
    c->head[h] = (int32_t)pos;
}

// Best (distance, length) at `pos`, or length 0 if nothing is worth encoding.
static void find_match(chain_t *c, const unsigned char *buf, size_t size,
                       size_t pos, size_t *best_dist, size_t *best_len)
{
    *best_dist = 0;
    *best_len = 0;

    if (pos + MIN_MATCH > size) return;

    size_t max_len = size - pos;
    if (max_len > MAX_MATCH) max_len = MAX_MATCH;

    size_t floor_pos = pos > WINDOW ? pos - WINDOW : 0;
    int32_t cand = c->head[hash3(buf + pos)];

    for (int depth = 0; depth < MAX_CHAIN && cand >= 0; depth++)
    {
        size_t at = (size_t)cand;
        if (at < floor_pos) break;            // and everything older is too

        size_t len = 0;
        while (len < max_len && buf[at + len] == buf[pos + len]) len++;

        if (len > *best_len)
        {
            *best_len = len;
            *best_dist = pos - at - 1;
            if (len >= max_len) break;
        }

        cand = c->prev[at & (WINDOW - 1)];
        if ((size_t)cand >= at) break;        // corrupt chain; refuse to loop
    }

    if (*best_len < MIN_MATCH) { *best_len = 0; *best_dist = 0; }
}

bool vp_yaz0_compress(const unsigned char *src, size_t src_size, vp_buf *out)
{
    out->data = NULL;
    out->size = 0;
    if (src_size == 0) return false;

    // Worst case is every byte a literal: one flag bit each, so 9 bytes per 8
    // input bytes, plus the header and a partial final group.
    size_t cap = YAZ0_HEADER + src_size + (src_size + 7) / 8 + 8;
    unsigned char *dst = (unsigned char *)malloc(cap);
    if (!dst) return false;

    chain_t chain;
    if (!chain_init(&chain)) { free(dst); return false; }

    memcpy(dst, "Yaz0", 4);
    write_be32(dst + 4, (uint32_t)src_size);
    memset(dst + 8, 0, 8);                    // both reserved words are 0 in every sample

    size_t o = YAZ0_HEADER;
    size_t pos = 0;

    while (pos < src_size)
    {
        size_t flag_at = o++;                 // the group's flag byte, filled in below
        unsigned flags = 0;
        int chunks = 0;

        while (chunks < 8 && pos < src_size)
        {
            size_t dist, len;
            find_match(&chain, src, src_size, pos, &dist, &len);

            flags <<= 1;

            if (len >= MIN_MATCH)
            {
                if (len <= SHORT_MATCH)
                {
                    dst[o++] = (unsigned char)(((len - 2) << 4) | (dist >> 8));
                    dst[o++] = (unsigned char)(dist & 0xFF);
                }
                else
                {
                    dst[o++] = (unsigned char)(dist >> 8);
                    dst[o++] = (unsigned char)(dist & 0xFF);
                    dst[o++] = (unsigned char)(len - 0x12);
                }
                for (size_t i = 0; i < len; i++)
                {
                    if (pos + MIN_MATCH <= src_size) chain_insert(&chain, src, pos);
                    pos++;
                }
            }
            else
            {
                flags |= 1;
                dst[o++] = src[pos];
                if (pos + MIN_MATCH <= src_size) chain_insert(&chain, src, pos);
                pos++;
            }
            chunks++;
        }

        // A short final group is left-justified: the flag bits describe the
        // chunks that are there, starting from the MSB.
        dst[flag_at] = (unsigned char)(flags << (8 - chunks));
    }

    chain_free(&chain);

    out->data = dst;
    out->size = o;
    return true;
}
