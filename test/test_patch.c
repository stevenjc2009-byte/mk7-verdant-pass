// Host test for the patch engine - the code that turns the player's own Mario
// Kart 7 files into the modded ones.
//
// This is the test that matters. The engine never runs against anything but
// the real game, so it is exercised here against the real ROM extract, and its
// output is checked against the files the PC toolchain produced when the track
// was built. If the two disagree, the console would be writing a track that
// was never verified.
//
// Reference files come from mk7-mod/tools/yaz0.py, which was itself verified
// against 3dstool - so the decompressor here is checked by something outside
// this codebase rather than by itself.
//
// Built and run by test/run.sh.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msbt.h"
#include "patch.h"
#include "sarc.h"
#include "szs.h"

static int s_checks, s_failures;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok) { s_failures++; printf("  FAIL  %s\n", what); }
}

static bool load(const char *path, vp_buf *out)
{
    out->data = NULL;
    out->size = 0;

    FILE *f = fopen(path, "rb");
    if (!f) { printf("  cannot open %s\n", path); return false; }

    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }

    out->data = (unsigned char *)malloc((size_t)n);
    if (!out->data) { fclose(f); return false; }

    out->size = fread(out->data, 1, (size_t)n, f);
    fclose(f);
    return out->size == (size_t)n;
}

static bool save(const char *path, const vp_buf *b)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(b->data, 1, b->size, f) == b->size;
    return fclose(f) == 0 && ok;
}

static bool same(const vp_buf *a, const vp_buf *b)
{
    return a->size == b->size && memcmp(a->data, b->data, a->size) == 0;
}

static void path2(char *out, size_t cap, const char *a, const char *b)
{
    snprintf(out, cap, "%s/%s", a, b);
}

// ---------------------------------------------------------------------------

static void test_yaz0_against_reference(void)
{
    char p[512];
    vp_buf stock, reference, mine;

    path2(p, sizeof(p), GAME_ROMFS, VP_COURSE_FILE);
    if (!load(p, &stock)) { check("stock course readable", 0); return; }

    path2(p, sizeof(p), REF_DIR, "stock_course.sarc");
    if (!load(p, &reference)) { check("python reference readable", 0); return; }

    check("decompress matches python's, byte for byte",
          vp_yaz0_decompress(stock.data, stock.size, &mine) && same(&mine, &reference));

    // Round trip on the real 3 MB archive, not on a toy string. This is the
    // one that would catch a compressor emitting a stream only it can read.
    vp_buf packed, back;
    if (vp_yaz0_compress(reference.data, reference.size, &packed))
    {
        check("our own compressor round-trips 3 MB",
              vp_yaz0_decompress(packed.data, packed.size, &back) &&
              same(&back, &reference));
        check("compressed smaller than stored-literal would be",
              packed.size < reference.size);
        printf("  yaz0: %zu -> %zu bytes (%.1f%%)\n", reference.size, packed.size,
               100.0 * (double)packed.size / (double)reference.size);
        vp_buf_free(&back);
        vp_buf_free(&packed);
    }
    else check("compressor ran", 0);

    vp_buf_free(&mine);
    vp_buf_free(&reference);
    vp_buf_free(&stock);
}

static void test_course(void)
{
    char p[512];
    vp_payload payload;
    vp_buf stock, out, raw, ref_built;

    path2(p, sizeof(p), VP_PAYLOAD_ROOT, "course.bcmdl");
    if (!load(p, &payload.cgfx)) { check("payload cgfx readable", 0); return; }
    path2(p, sizeof(p), VP_PAYLOAD_ROOT, "course.kcl");
    if (!load(p, &payload.kcl))  { check("payload kcl readable", 0); return; }
    path2(p, sizeof(p), VP_PAYLOAD_ROOT, "course.kmp");
    if (!load(p, &payload.kmp))  { check("payload kmp readable", 0); return; }

    path2(p, sizeof(p), GAME_ROMFS, VP_COURSE_FILE);
    if (!load(p, &stock)) { check("stock course readable", 0); return; }

    if (!vp_patch_course(stock.data, stock.size, &payload, &out))
    {
        printf("  vp_patch_course: %s\n", vp_patch_error());
        check("course patch succeeded", 0);
        return;
    }
    check("course patch succeeded", 1);

    path2(p, sizeof(p), OUT_DIR, "course.szs");
    check("course output written", save(p, &out));

    check("course output decompresses", vp_yaz0_decompress(out.data, out.size, &raw));
    path2(p, sizeof(p), OUT_DIR, "course.sarc");
    check("course archive written for the python cross-check", save(p, &raw));

    // The PC toolchain built this same archive when the track was made. Byte
    // equality means the console will write exactly what was verified there.
    path2(p, sizeof(p), REF_DIR, "built_course.sarc");
    if (load(p, &ref_built))
        check("rebuilt archive is byte-identical to the PC toolchain's", same(&raw, &ref_built));
    else
        check("PC-built reference readable", 0);

    // And independently of that: our three entries in, everything else theirs.
    vp_sarc mine, stock_arc;
    vp_buf stock_raw;
    if (vp_yaz0_decompress(stock.data, stock.size, &stock_raw) &&
        vp_sarc_read(raw.data, raw.size, &mine) &&
        vp_sarc_read(stock_raw.data, stock_raw.size, &stock_arc))
    {
        check("entry count unchanged", mine.count == stock_arc.count);

        int replaced = 0, untouched = 0;
        for (int i = 0; i < mine.count; i++)
        {
            const vp_sarc_entry *ours = &mine.entries[i];
            const vp_sarc_entry *theirs = vp_sarc_find(&stock_arc, ours->hash);
            if (!theirs) { check("every entry hash exists in the stock file", 0); continue; }

            bool is_ours = ours->hash == VP_HASH_CGFX ||
                           ours->hash == VP_HASH_KCL  ||
                           ours->hash == VP_HASH_KMP;
            if (is_ours) replaced++;
            else if (ours->size == theirs->size &&
                     memcmp(ours->data, theirs->data, ours->size) == 0) untouched++;
        }
        check("our three entries are in", replaced == 3);
        check("the other twelve are Nintendo's, untouched", untouched == 12);

        const vp_sarc_entry *e;
        e = vp_sarc_find(&mine, VP_HASH_CGFX);
        check("model entry is our file", e && e->size == payload.cgfx.size &&
              memcmp(e->data, payload.cgfx.data, e->size) == 0);
        e = vp_sarc_find(&mine, VP_HASH_KCL);
        check("collision entry is our file", e && e->size == payload.kcl.size &&
              memcmp(e->data, payload.kcl.data, e->size) == 0);
        e = vp_sarc_find(&mine, VP_HASH_KMP);
        check("layout entry is our file", e && e->size == payload.kmp.size &&
              memcmp(e->data, payload.kmp.data, e->size) == 0);

        vp_buf_free(&stock_raw);
    }
    else check("archives parse for comparison", 0);

    // A file that is not the game's course must be refused, not patched blind.
    // Someone running this against a modified or wrong-region game should get
    // a message, not a corrupt track.
    vp_buf junk;
    unsigned char not_an_archive[64];
    memset(not_an_archive, 0xAA, sizeof(not_an_archive));
    check("a file that is not a .szs is refused",
          !vp_patch_course(not_an_archive, sizeof(not_an_archive), &payload, &junk));

    // A real archive that simply does not contain the course entries - which
    // is what a different game would look like - is refused too.
    vp_buf ui_stock;
    path2(p, sizeof(p), GAME_ROMFS, VP_UI_FILES[1]);
    if (load(p, &ui_stock))
    {
        check("an archive without the course entries is refused",
              !vp_patch_course(ui_stock.data, ui_stock.size, &payload, &junk));
        vp_buf_free(&ui_stock);
    }

    vp_buf_free(&ref_built);
    vp_buf_free(&raw);
    vp_buf_free(&out);
    vp_buf_free(&stock);
    vp_buf_free(&payload.cgfx);
    vp_buf_free(&payload.kcl);
    vp_buf_free(&payload.kmp);
}

static void test_ui(void)
{
    for (int i = 0; i < VP_UI_FILE_COUNT; i++)
    {
        char p[512], label[256];
        vp_buf stock, out, raw, stock_raw;

        path2(p, sizeof(p), GAME_ROMFS, VP_UI_FILES[i]);
        if (!load(p, &stock))
        {
            snprintf(label, sizeof(label), "%s readable", VP_UI_FILES[i]);
            check(label, 0);
            continue;
        }

        if (!vp_patch_ui(stock.data, stock.size, &out))
        {
            printf("  vp_patch_ui(%s): %s\n", VP_UI_FILES[i], vp_patch_error());
            snprintf(label, sizeof(label), "%s patched", VP_UI_FILES[i]);
            check(label, 0);
            vp_buf_free(&stock);
            continue;
        }

        // Written out for the python cross-check, which reads the name back
        // with the same parser that built the reference track.
        snprintf(p, sizeof(p), "%s/ui_%d.szs", OUT_DIR, i);
        snprintf(label, sizeof(label), "%s output written", VP_UI_FILES[i]);
        check(label, save(p, &out));

        if (vp_yaz0_decompress(out.data, out.size, &raw) &&
            vp_yaz0_decompress(stock.data, stock.size, &stock_raw))
        {
            snprintf(p, sizeof(p), "%s/ui_%d.sarc", OUT_DIR, i);
            save(p, &raw);

            vp_sarc mine, theirs;
            if (vp_sarc_read(raw.data, raw.size, &mine) &&
                vp_sarc_read(stock_raw.data, stock_raw.size, &theirs))
            {
                snprintf(label, sizeof(label), "%s: entry count unchanged", VP_UI_FILES[i]);
                check(label, mine.count == theirs.count);

                // Exactly one entry may differ. A language file holds the whole
                // UI; touching a second one would be corruption nobody would
                // notice until the game was running.
                int differ = 0;
                for (int k = 0; k < mine.count; k++)
                {
                    const vp_sarc_entry *t = vp_sarc_find(&theirs, mine.entries[k].hash);
                    if (!t) { differ = 99; break; }
                    if (mine.entries[k].size != t->size ||
                        memcmp(mine.entries[k].data, t->data, t->size) != 0) differ++;
                }
                snprintf(label, sizeof(label), "%s: exactly one entry changed", VP_UI_FILES[i]);
                check(label, differ == 1);

                const vp_sarc_entry *e = vp_sarc_find(&mine, VP_HASH_MSBT);
                char name[128] = { 0 };
                bool got = e && vp_msbt_get_string(e->data, e->size, VP_NAME_INDEX,
                                                   name, sizeof(name));
                snprintf(label, sizeof(label), "%s: reads back \"%s\"", VP_UI_FILES[i], name);
                check(label, got && strcmp(name, VP_COURSE_NAME) == 0);
            }
            else
            {
                snprintf(label, sizeof(label), "%s: archives parse", VP_UI_FILES[i]);
                check(label, 0);
            }
            vp_buf_free(&stock_raw);
            vp_buf_free(&raw);
        }

        vp_buf_free(&out);
        vp_buf_free(&stock);
    }
}

int main(void)
{
    printf("patch engine\n");
    test_yaz0_against_reference();
    test_course();
    test_ui();
    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
