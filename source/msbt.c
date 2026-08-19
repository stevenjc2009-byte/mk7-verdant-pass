#include "msbt.h"

#include <stdlib.h>
#include <string.h>

#define SECTION_START   0x20
#define SECTION_HEADER  0x10
#define SECTION_ALIGN   16
#define SECTION_PAD     0xAB     // Nintendo pads sections with this, not zero
#define FILE_SIZE_AT    0x12

typedef struct {
    size_t hdr;      // offset of the section header
    size_t data;     // offset of its payload
    size_t size;     // payload size, from the header
} section_t;

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;         p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static bool is_magic_char(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

// Walks the section chain looking for TXT2. Sections are 16-byte aligned.
static bool find_txt2(const unsigned char *buf, size_t size, section_t *out)
{
    if (size < SECTION_START + SECTION_HEADER) return false;
    if (memcmp(buf, "MsgStdBn", 8) != 0) return false;
    if (!(buf[8] == 0xFF && buf[9] == 0xFE)) return false;   // little-endian

    size_t off = SECTION_START;
    while (off + SECTION_HEADER <= size)
    {
        if (!is_magic_char(buf[off]) || !is_magic_char(buf[off + 1]) ||
            !is_magic_char(buf[off + 2]) || !is_magic_char(buf[off + 3]))
            break;

        size_t sec_size = rd32(buf + off + 4);
        if (sec_size > size || off + SECTION_HEADER + sec_size > size) return false;

        if (memcmp(buf + off, "TXT2", 4) == 0)
        {
            out->hdr = off;
            out->data = off + SECTION_HEADER;
            out->size = sec_size;
            return true;
        }

        off += SECTION_HEADER + sec_size;
        off = (off + (SECTION_ALIGN - 1)) & ~(size_t)(SECTION_ALIGN - 1);
    }
    return false;
}

// TXT2 payload: u32 count, then count u32 offsets from the payload start, then
// the UTF-16LE strings packed back to back.
static bool txt2_spans(const unsigned char *buf, const section_t *txt2,
                       int index, size_t *begin, size_t *end, uint32_t *count_out)
{
    if (txt2->size < 4) return false;
    uint32_t count = rd32(buf + txt2->data);
    if (count == 0 || (size_t)count > (txt2->size - 4) / 4) return false;
    if (index < 0 || (uint32_t)index >= count) return false;

    if (count_out) *count_out = count;
    if (!begin) return true;

    uint32_t o    = rd32(buf + txt2->data + 4 + 4 * (size_t)index);
    uint32_t next = ((uint32_t)index + 1 < count)
                  ? rd32(buf + txt2->data + 4 + 4 * (size_t)index + 4)
                  : (uint32_t)txt2->size;

    if (o > next || next > txt2->size) return false;
    *begin = o;
    *end = next;
    return true;
}

bool vp_msbt_get_string(const unsigned char *buf, size_t size,
                        int index, char *out, size_t cap)
{
    section_t txt2;
    if (!find_txt2(buf, size, &txt2)) return false;

    size_t begin, end;
    if (!txt2_spans(buf, &txt2, index, &begin, &end, NULL)) return false;

    const unsigned char *s = buf + txt2.data + begin;
    size_t units = (end - begin) / 2;
    size_t written = 0;

    for (size_t i = 0; i < units && written + 1 < cap; i++)
    {
        unsigned code = (unsigned)s[i * 2] | ((unsigned)s[i * 2 + 1] << 8);
        if (code == 0) break;
        out[written++] = (code < 0x80) ? (char)code : '?';
    }
    if (cap) out[written < cap ? written : cap - 1] = '\0';
    return true;
}

bool vp_msbt_set_string(const unsigned char *buf, size_t size,
                        int index, const char *text, vp_buf *out)
{
    out->data = NULL;
    out->size = 0;

    section_t txt2;
    if (!find_txt2(buf, size, &txt2)) return false;

    uint32_t count = 0;
    if (!txt2_spans(buf, &txt2, index, NULL, NULL, &count)) return false;

    size_t text_len = strlen(text);
    for (size_t i = 0; i < text_len; i++)
        if ((unsigned char)text[i] >= 0x80) return false;   // see the header

    size_t new_blob = (text_len + 1) * 2;   // UTF-16LE plus its terminator

    // New payload: the same table, then every old blob except `index`, which
    // is swapped for ours. Offsets are recomputed from scratch.
    size_t table = 4 + 4 * (size_t)count;
    size_t payload_size = table;

    for (uint32_t i = 0; i < count; i++)
    {
        size_t b, e;
        if (!txt2_spans(buf, &txt2, (int)i, &b, &e, NULL)) return false;
        payload_size += ((int)i == index) ? new_blob : (e - b);
    }

    size_t new_txt2_total = SECTION_HEADER + payload_size;
    size_t pad = (SECTION_ALIGN - (new_txt2_total % SECTION_ALIGN)) % SECTION_ALIGN;
    new_txt2_total += pad;

    size_t old_end = txt2.data + txt2.size;
    old_end = (old_end + (SECTION_ALIGN - 1)) & ~(size_t)(SECTION_ALIGN - 1);
    if (old_end > size) return false;
    size_t tail_size = size - old_end;

    size_t total = txt2.hdr + new_txt2_total + tail_size;
    unsigned char *dst = (unsigned char *)malloc(total);
    if (!dst) return false;

    memcpy(dst, buf, txt2.hdr);                                   // everything before TXT2

    unsigned char *sec = dst + txt2.hdr;
    memcpy(sec, "TXT2", 4);
    wr32(sec + 4, (uint32_t)payload_size);
    // Bytes 8..0x0F of the section header are whatever Nintendo put there;
    // carry them through rather than assuming they are zero.
    memcpy(sec + 8, buf + txt2.hdr + 8, SECTION_HEADER - 8);

    unsigned char *payload = sec + SECTION_HEADER;
    wr32(payload, count);

    size_t run = table;
    for (uint32_t i = 0; i < count; i++)
    {
        wr32(payload + 4 + 4 * (size_t)i, (uint32_t)run);

        if ((int)i == index)
        {
            for (size_t c = 0; c < text_len; c++)
            {
                payload[run + c * 2]     = (unsigned char)text[c];
                payload[run + c * 2 + 1] = 0;
            }
            payload[run + text_len * 2]     = 0;
            payload[run + text_len * 2 + 1] = 0;
            run += new_blob;
        }
        else
        {
            size_t b, e;
            if (!txt2_spans(buf, &txt2, (int)i, &b, &e, NULL)) { free(dst); return false; }
            memcpy(payload + run, buf + txt2.data + b, e - b);
            run += e - b;
        }
    }

    memset(sec + SECTION_HEADER + payload_size, SECTION_PAD, pad);
    memcpy(sec + new_txt2_total, buf + old_end, tail_size);

    wr32(dst + FILE_SIZE_AT, (uint32_t)total);

    out->data = dst;
    out->size = total;
    return true;
}
