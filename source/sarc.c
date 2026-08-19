#include "sarc.h"

#include <stdlib.h>
#include <string.h>

#define HEADER_LEN      0x14
#define SFAT_HEADER_LEN 0x0C
#define SFNT_HEADER_LEN 0x08
#define DATA_ALIGN      0x80
#define VERSION         0x0100
#define HASH_KEY        0x65
#define NAMED_BIT       0x01000000u

static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }

static void wr32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;         p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static size_t align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

bool vp_sarc_read(const unsigned char *buf, size_t size, vp_sarc *out)
{
    out->count = 0;

    if (size < HEADER_LEN || memcmp(buf, "SARC", 4) != 0) return false;

    // Little-endian only. MK7 is, and a big-endian archive here would mean the
    // file came from somewhere this app has no business patching.
    if (!(buf[6] == 0xFF && buf[7] == 0xFE)) return false;

    size_t header_len = rd16(buf + 4);
    size_t data_start = rd32(buf + 12);
    if (header_len + SFAT_HEADER_LEN > size) return false;

    const unsigned char *sfat = buf + header_len;
    if (memcmp(sfat, "SFAT", 4) != 0) return false;

    size_t sfat_header_len = rd16(sfat + 4);
    int count = rd16(sfat + 6);
    if (count <= 0 || count > VP_SARC_MAX_ENTRIES) return false;

    size_t nodes_off = header_len + sfat_header_len;
    if (nodes_off + (size_t)count * 16 > size) return false;

    for (int i = 0; i < count; i++)
    {
        const unsigned char *node = buf + nodes_off + (size_t)i * 16;
        uint32_t attrs = rd32(node + 4);
        uint32_t begin = rd32(node + 8);
        uint32_t end   = rd32(node + 12);

        // A named entry would need the SFNT table carried through the rebuild.
        // No MK7 archive has one, so rather than write code that has never
        // been exercised, refuse.
        if (attrs & NAMED_BIT) return false;
        if (end < begin) return false;
        if (data_start + end > size) return false;

        out->entries[i].hash = rd32(node);
        out->entries[i].data = buf + data_start + begin;
        out->entries[i].size = (size_t)(end - begin);
    }

    out->count = count;
    return true;
}

const vp_sarc_entry *vp_sarc_find(const vp_sarc *arc, uint32_t hash)
{
    for (int i = 0; i < arc->count; i++)
        if (arc->entries[i].hash == hash) return &arc->entries[i];
    return NULL;
}

bool vp_sarc_replace(vp_sarc *arc, uint32_t hash,
                     const unsigned char *data, size_t size)
{
    for (int i = 0; i < arc->count; i++)
    {
        if (arc->entries[i].hash != hash) continue;
        arc->entries[i].data = data;
        arc->entries[i].size = size;
        return true;
    }
    return false;
}

bool vp_sarc_write(const vp_sarc *arc, vp_buf *out)
{
    out->data = NULL;
    out->size = 0;
    if (arc->count <= 0) return false;

    // Laid out back to back: header, SFAT header, the nodes, SFNT header, then
    // the name table - which is empty here, because the archive is nameless.
    size_t nodes_at    = HEADER_LEN + SFAT_HEADER_LEN;
    size_t sfnt_at     = nodes_at + (size_t)arc->count * 16;
    size_t names_start = sfnt_at + SFNT_HEADER_LEN;
    size_t data_start  = align_up(names_start, DATA_ALIGN);

    // begin[0] = 0, and every following entry starts on the next 0x80
    // boundary after the previous one ended. Measured on every real archive.
    size_t total = 0;
    for (int i = 0; i < arc->count; i++)
    {
        if (i) total = align_up(total, DATA_ALIGN);
        total += arc->entries[i].size;
    }

    size_t file_len = data_start + total;
    unsigned char *dst = (unsigned char *)calloc(1, file_len);
    if (!dst) return false;

    memcpy(dst, "SARC", 4);
    wr16(dst + 4, HEADER_LEN);
    dst[6] = 0xFF; dst[7] = 0xFE;
    wr32(dst + 8, (uint32_t)file_len);
    wr32(dst + 12, (uint32_t)data_start);
    wr16(dst + 16, VERSION);
    wr16(dst + 18, 0);

    memcpy(dst + HEADER_LEN, "SFAT", 4);
    wr16(dst + HEADER_LEN + 4, SFAT_HEADER_LEN);
    wr16(dst + HEADER_LEN + 6, (uint16_t)arc->count);
    wr32(dst + HEADER_LEN + 8, HASH_KEY);

    size_t begin = 0;
    for (int i = 0; i < arc->count; i++)
    {
        if (i) begin = align_up(begin, DATA_ALIGN);
        size_t end = begin + arc->entries[i].size;

        unsigned char *node = dst + nodes_at + (size_t)i * 16;
        wr32(node,      arc->entries[i].hash);
        wr32(node + 4,  0);                       // nameless: no bit, no offset
        wr32(node + 8,  (uint32_t)begin);
        wr32(node + 12, (uint32_t)end);

        memcpy(dst + data_start + begin, arc->entries[i].data, arc->entries[i].size);
        begin = end;
    }

    memcpy(dst + sfnt_at, "SFNT", 4);
    wr16(dst + sfnt_at + 4, SFNT_HEADER_LEN);
    wr16(dst + sfnt_at + 6, 0);

    out->data = dst;
    out->size = file_len;
    return true;
}
