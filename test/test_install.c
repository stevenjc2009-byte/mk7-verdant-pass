// Host test for the install layer - everything around the patch engine.
//
// The patch engine is proven separately by test_patch.c, byte for byte against
// the archives the PC toolchain built. What is left is the part that decides
// *where* things go and *when* to refuse: the Hotswap folder layout, the
// mkdir -p, the read-back after every write, and the refusal to write while
// Hotswap has the mod swapped in.
//
// This runs the real vp_install against the real ROM extract and the real
// shipped payload. A stand-in payload would prove nothing about what installs.
//
// It does NOT cover romfsMountFromTitle, the console front end, or AM. Those
// need the console.
//
// Build and run with test/run.sh. The roots are not passed as -D on the command
// line: MSYS mangles a quoted path inside a -D, which fails as a "missing
// terminating " character" error a long way from its cause. run.sh writes them
// into a generated header and force-includes that instead.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "install.h"
#include "patch.h"
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
    if (!f) return false;

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

static long file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

// Writes a stand-in Hotswap state file, to drive the refusal checks.
static void write_state(const char *contents)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/hotswap/%s/state", SD_ROOT_DIR, VP_GAME_SLUG);

    FILE *f = fopen(path, "wb");
    if (!f) { printf("  cannot write %s\n", path); return; }
    fputs(contents, f);
    fclose(f);
}

static void remove_state(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/hotswap/%s/state", SD_ROOT_DIR, VP_GAME_SLUG);
    remove(path);
}

static int s_progress_calls;
static int s_progress_total;
static bool s_progress_ordered = true;

static void on_progress(const char *name, int done, int total, void *user)
{
    (void)user;
    if (!name || !name[0]) s_progress_ordered = false;
    if (done != s_progress_calls) s_progress_ordered = false;   // one per file, in order
    s_progress_calls++;
    s_progress_total = total;
}

// The finished file on the card must decompress to the archive the PC toolchain
// built. Comparing the .szs bytes instead would only be comparing compressors.
static void check_written(const char *dest_root, const char *rel, const char *ref)
{
    char path[512], label[256];
    vp_buf on_card, raw, want;

    snprintf(path, sizeof(path), "%s/%s", dest_root, rel);
    snprintf(label, sizeof(label), "%s written", rel);
    if (!load(path, &on_card)) { check(label, 0); return; }
    check(label, 1);

    snprintf(path, sizeof(path), "%s/%s", REF_DIR, ref);
    snprintf(label, sizeof(label), "%s matches the PC toolchain's archive", rel);
    if (!load(path, &want)) { check(label, 0); vp_buf_free(&on_card); return; }

    check(label, vp_yaz0_decompress(on_card.data, on_card.size, &raw) &&
                 raw.size == want.size && memcmp(raw.data, want.data, raw.size) == 0);

    vp_buf_free(&raw);
    vp_buf_free(&want);
    vp_buf_free(&on_card);
}

int main(void)
{
    printf("install engine\n");

    char dest[512];
    vp_dest_path(dest, sizeof(dest));

    // The folder Hotswap will show, in the layout it expects: the mod's name is
    // the directory name, and the files sit under layeredfs/romfs.
    char want_dest[512];
    snprintf(want_dest, sizeof(want_dest),
             "%s/hotswap/%s/%s/layeredfs/romfs", SD_ROOT_DIR, VP_GAME_SLUG, VP_MOD_SLUG);
    check("destination is the Hotswap parked path", strcmp(dest, want_dest) == 0);

    check("nine files to write: the course and eight languages",
          vp_step_count() == 1 + VP_UI_FILE_COUNT);
    check("nothing installed yet", !vp_is_installed());
    check("nothing swapped in yet", !vp_mod_is_active());

    // --- the install ---------------------------------------------------------

    int files = -1;
    unsigned long long bytes = 0;
    vp_result_t r = vp_install(on_progress, NULL, &files, &bytes);

    if (r != VP_OK) printf("  vp_install: %s\n", vp_result_str(r));
    check("install succeeded", r == VP_OK);
    check("wrote nine files", files == vp_step_count());
    check("wrote some bytes", bytes > 1000000);
    check("progress fired once per file", s_progress_calls == vp_step_count());
    check("progress reported the right total", s_progress_total == vp_step_count());
    check("progress counted up in order", s_progress_ordered);
    check("installed now", vp_is_installed());

    printf("  wrote %d files, %llu bytes\n", files, bytes);

    check_written(dest, VP_COURSE_FILE, "built_course.sarc");
    for (int i = 0; i < VP_UI_FILE_COUNT; i++)
    {
        char ref[64];
        snprintf(ref, sizeof(ref), "built_ui_%d.sarc", i);
        check_written(dest, VP_UI_FILES[i], ref);
    }

    // --- reinstalling --------------------------------------------------------

    // Overwrite, not append. Opening the destination in the wrong mode would
    // double every file and MK7 would fail to load the track, so the size is
    // recorded and compared rather than the install just being re-run.
    char course_path[1024];
    snprintf(course_path, sizeof(course_path), "%s/%s", dest, VP_COURSE_FILE);
    long first = file_size(course_path);

    s_progress_calls = 0;
    r = vp_install(NULL, NULL, &files, &bytes);
    check("reinstall succeeded", r == VP_OK);
    check("reinstall did not append", file_size(course_path) == first);
    check("reinstall wrote the same nine files", files == vp_step_count());

    // --- refusing while Hotswap has it swapped in ----------------------------

    write_state("active=" VP_MOD_SLUG "\nlayeredfs=" VP_MOD_SLUG "\nplugins=\n");
    check("sees its own mod swapped in", vp_mod_is_active());
    check("refuses to write underneath Hotswap",
          vp_install(NULL, NULL, NULL, NULL) == VP_ERR_ACTIVE);

    // Another mod owning the slot is not us.
    write_state("active=ctgp7\nlayeredfs=ctgp7\nplugins=ctgp7\n");
    check("another mod in the slot is not us", !vp_mod_is_active());

    // A whole-value match, not a prefix one: a folder called "Verdant Pass
    // Extra" is a different mod, and treating it as ours would refuse an
    // install the player is entitled to.
    write_state("active=Verdant Pass Extra\nlayeredfs=Verdant Pass Extra\nplugins=\n");
    check("a longer name that starts the same is not us", !vp_mod_is_active());

    // An older Hotswap wrote no per-slot lines at all.
    write_state("active=" VP_MOD_SLUG "\n");
    check("a state file with no layeredfs line is not a match", !vp_mod_is_active());

    remove_state();
    check("no state file means nothing is swapped in", !vp_mod_is_active());
    check("installs again once the slot is free",
          vp_install(NULL, NULL, NULL, NULL) == VP_OK);

    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
