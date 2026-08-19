#include "patch.h"

#include <stdlib.h>
#include <string.h>

#include "msbt.h"
#include "sarc.h"

const char *const VP_UI_FILES[] = {
    "UI/common-ed.szs",   // German
    "UI/common-ee.szs",   // English
    "UI/common-ef.szs",   // French
    "UI/common-ei.szs",   // Italian
    "UI/common-en.szs",   // Dutch
    "UI/common-ep.szs",   // Portuguese
    "UI/common-er.szs",   // Russian
    "UI/common-es.szs",   // Spanish
};
const int VP_UI_FILE_COUNT = (int)(sizeof(VP_UI_FILES) / sizeof(VP_UI_FILES[0]));

static const char *s_error = "";

const char *vp_patch_error(void) { return s_error; }

bool vp_patch_course(const unsigned char *stock_szs, size_t stock_size,
                     const vp_payload *payload, vp_buf *out)
{
    out->data = NULL;
    out->size = 0;
    s_error = "";

    vp_buf raw;
    if (!vp_yaz0_decompress(stock_szs, stock_size, &raw))
    {
        s_error = "The game's course file could not be decompressed.";
        return false;
    }

    vp_sarc arc;
    if (!vp_sarc_read(raw.data, raw.size, &arc))
    {
        s_error = "The game's course file is not an archive this app understands.";
        vp_buf_free(&raw);
        return false;
    }

    // All three or none. A partial substitution would produce a course whose
    // collision and model disagree, which is a track that loads and then drops
    // the player through the floor - far worse than refusing here.
    bool ok = vp_sarc_replace(&arc, VP_HASH_CGFX, payload->cgfx.data, payload->cgfx.size)
           && vp_sarc_replace(&arc, VP_HASH_KCL,  payload->kcl.data,  payload->kcl.size)
           && vp_sarc_replace(&arc, VP_HASH_KMP,  payload->kmp.data,  payload->kmp.size);

    if (!ok)
    {
        s_error = "This copy of Mario Kart 7 does not have the course files "
                  "this track was built against.";
        vp_buf_free(&raw);
        return false;
    }

    vp_buf rebuilt;
    if (!vp_sarc_write(&arc, &rebuilt))
    {
        s_error = "Ran out of memory rebuilding the course archive.";
        vp_buf_free(&raw);
        return false;
    }
    vp_buf_free(&raw);            // the entries were copied out by now

    bool packed = vp_yaz0_compress(rebuilt.data, rebuilt.size, out);
    vp_buf_free(&rebuilt);

    if (!packed) s_error = "Ran out of memory compressing the course archive.";
    return packed;
}

bool vp_patch_ui(const unsigned char *stock_szs, size_t stock_size, vp_buf *out)
{
    out->data = NULL;
    out->size = 0;
    s_error = "";

    vp_buf raw;
    if (!vp_yaz0_decompress(stock_szs, stock_size, &raw))
    {
        s_error = "A game language file could not be decompressed.";
        return false;
    }

    vp_sarc arc;
    if (!vp_sarc_read(raw.data, raw.size, &arc))
    {
        s_error = "A game language file is not an archive this app understands.";
        vp_buf_free(&raw);
        return false;
    }

    const vp_sarc_entry *msbt = vp_sarc_find(&arc, VP_HASH_MSBT);
    if (!msbt)
    {
        s_error = "A game language file has no course-name table.";
        vp_buf_free(&raw);
        return false;
    }

    vp_buf renamed;
    if (!vp_msbt_set_string(msbt->data, msbt->size, VP_NAME_INDEX,
                            VP_COURSE_NAME, &renamed))
    {
        s_error = "The course name could not be rewritten.";
        vp_buf_free(&raw);
        return false;
    }

    if (!vp_sarc_replace(&arc, VP_HASH_MSBT, renamed.data, renamed.size))
    {
        s_error = "The renamed table could not be put back.";
        vp_buf_free(&renamed);
        vp_buf_free(&raw);
        return false;
    }

    vp_buf rebuilt;
    bool built = vp_sarc_write(&arc, &rebuilt);
    vp_buf_free(&renamed);
    vp_buf_free(&raw);

    if (!built)
    {
        s_error = "Ran out of memory rebuilding a language file.";
        return false;
    }

    bool packed = vp_yaz0_compress(rebuilt.data, rebuilt.size, out);
    vp_buf_free(&rebuilt);

    if (!packed) s_error = "Ran out of memory compressing a language file.";
    return packed;
}
