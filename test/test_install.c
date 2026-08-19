// Host test for the install engine.
//
// install.c is plain POSIX file IO, so the same source that runs on the console
// compiles and runs here with the two roots pointed at a temporary directory.
// That covers the parts that can actually go wrong quietly - the recursive walk,
// mkdir -p, the copy, the size read-back, and reading Hotswap's state file.
//
// It does NOT cover romfsInit, the console front end, or AM. Those need the
// console.
//
// Build and run with test/run.sh. The three roots are not passed as -D on the
// command line: MSYS mangles a quoted path inside a -D, which fails as a
// "missing terminating" character" error a long way from its cause. run.sh
// writes them into a generated header and force-includes that instead.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#define make_dir(p) mkdir((p), 0777)
#endif

#include "install.h"

static int s_checks, s_failures;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok)
    {
        s_failures++;
        printf("  FAIL  %s\n", what);
    }
}

static void write_file(const char *path, size_t bytes, unsigned char seed)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("  cannot create %s\n", path); exit(2); }

    for (size_t i = 0; i < bytes; i++) fputc((int)((i + seed) & 0xff), f);
    fclose(f);
}

static long file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

// Byte-for-byte, not just the size the installer itself checks. The installer
// verifying its own copy would pass even if the copy loop were wrong in a way
// that preserved length, so the test compares content independently.
static int same_bytes(const char *a, const char *b)
{
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    if (!fa || !fb) { if (fa) fclose(fa); if (fb) fclose(fb); return 0; }

    int same = 1, ca, cb;
    do {
        ca = fgetc(fa);
        cb = fgetc(fb);
        if (ca != cb) { same = 0; break; }
    } while (ca != EOF);

    fclose(fa);
    fclose(fb);
    return same;
}

static int s_progress_calls;

static void on_progress(const char *name, int done, int total, void *user)
{
    (void)name; (void)user;
    s_progress_calls++;
    check("progress index is inside the total", done >= 0 && done < total);
}

// Builds a stand-in payload with the same shape as the real one: files at the
// top level and files one directory down, because the real payload is
// Course/Gn64_KalimariDesert.szs plus UI/common-*.szs.
static void setup(void)
{
    char path[640];

    make_dir(SD_ROOT_DIR);          // stands in for the SD card itself

    make_dir(VP_PAYLOAD_ROOT);
    snprintf(path, sizeof(path), "%s/Course", VP_PAYLOAD_ROOT);
    make_dir(path);
    snprintf(path, sizeof(path), "%s/UI", VP_PAYLOAD_ROOT);
    make_dir(path);

    snprintf(path, sizeof(path), "%s/flat.bin", VP_PAYLOAD_ROOT);
    write_file(path, 1000, 7);
    snprintf(path, sizeof(path), "%s/Course/course.szs", VP_PAYLOAD_ROOT);
    write_file(path, 40000, 19);
    snprintf(path, sizeof(path), "%s/UI/common-ee.szs", VP_PAYLOAD_ROOT);
    write_file(path, 2500, 33);
}

int main(void)
{
    printf("install engine\n");
    setup();

    // --- the payload the harness was pointed at -----------------------------
    int files = 0;
    unsigned long long bytes = 0;
    check("payload is readable", vp_payload_stat(&files, &bytes));
    check("payload has the 3 files the harness made", files == 3);
    check("payload byte total is right", bytes == 1000 + 2500 + 40000);

    // --- nothing installed yet ---------------------------------------------
    check("reports not installed before installing", !vp_is_installed());
    check("no state file means not active", !vp_mod_is_active());

    // --- install ------------------------------------------------------------
    int written = 0;
    unsigned long long written_bytes = 0;
    vp_result_t r = vp_install(on_progress, NULL, &written, &written_bytes);

    check("install succeeded", r == VP_OK);
    check("wrote every file", written == 3);
    check("wrote every byte", written_bytes == bytes);
    check("progress fired once per file", s_progress_calls == 3);
    check("reports installed afterwards", vp_is_installed());

    char dest[512];
    vp_dest_path(dest, sizeof(dest));

    char path[640], src[640];

    // Top-level file.
    snprintf(path, sizeof(path), "%s/flat.bin", dest);
    snprintf(src, sizeof(src), "%s/flat.bin", VP_PAYLOAD_ROOT);
    check("top-level file landed", file_size(path) == 1000);
    check("top-level file is byte-identical", same_bytes(src, path));

    // Nested one level - this is what proves mkdir -p ran for a subdirectory
    // that did not exist, which is the whole Course/ and UI/ case.
    snprintf(path, sizeof(path), "%s/Course/course.szs", dest);
    snprintf(src, sizeof(src), "%s/Course/course.szs", VP_PAYLOAD_ROOT);
    check("nested file landed", file_size(path) == 40000);
    check("nested file is byte-identical", same_bytes(src, path));

    snprintf(path, sizeof(path), "%s/UI/common-ee.szs", dest);
    check("second nested dir landed", file_size(path) == 2500);

    // --- reinstall over the top --------------------------------------------
    // The normal case when a player runs a newer version, and the one that
    // would break if the copy opened files with "ab" or refused to overwrite.
    r = vp_install(NULL, NULL, &written, &written_bytes);
    check("reinstall succeeded", r == VP_OK);
    check("reinstall wrote every file again", written == 3);
    snprintf(path, sizeof(path), "%s/Course/course.szs", dest);
    check("reinstall did not append", file_size(path) == 40000);

    // --- Hotswap says the mod is swapped in ---------------------------------
    char state[640];
    snprintf(state, sizeof(state), "%s%s/%s/state", SD_ROOT, "hotswap", VP_GAME_SLUG);

    FILE *f = fopen(state, "wb");
    check("could write a fake state file", f != NULL);
    if (f)
    {
        fputs("active=Verdant Pass\nlayeredfs=Verdant Pass\nplugins=-\n", f);
        fclose(f);
    }
    check("detects the mod is active", vp_mod_is_active());
    check("refuses to install while active",
          vp_install(NULL, NULL, NULL, NULL) == VP_ERR_ACTIVE);

    // A different mod owning the slot must NOT read as ours. This is the check
    // that would fail if the comparison were a prefix match rather than a whole
    // -value one.
    f = fopen(state, "wb");
    if (f)
    {
        fputs("active=ctgp7\nlayeredfs=ctgp7\nplugins=ctgp7\n", f);
        fclose(f);
    }
    check("another mod owning the slot is not us", !vp_mod_is_active());
    check("installs again once we do not own the slot",
          vp_install(NULL, NULL, NULL, NULL) == VP_OK);

    // And a value that merely starts the same must not match either.
    f = fopen(state, "wb");
    if (f)
    {
        fputs("active=-\nlayeredfs=Verdant Pass Extra\nplugins=-\n", f);
        fclose(f);
    }
    check("a longer name starting the same is not us", !vp_mod_is_active());

    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
