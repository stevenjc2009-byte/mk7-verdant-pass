#include "install.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// The console's mkdir is the POSIX two-argument one; MinGW's is one argument.
// The host test harness compiles this exact file, so the difference is bridged
// here. It cannot be bridged from the harness with a macro: a `mkdir(p,m)`
// macro would also try to rewrite the one-argument declaration in the system
// header. Inert on the console, where _WIN32 is never defined.
#ifdef _WIN32
#include <direct.h>
#define vp_mkdir(path) _mkdir(path)
#else
#define vp_mkdir(path) mkdir((path), 0777)
#endif

// Overridable for the host test harness, same reason as VP_PAYLOAD_ROOT.
#ifndef SD_ROOT
#define SD_ROOT "sdmc:/"
#endif
#define HOTSWAP_ROOT SD_ROOT "hotswap"

// 64KB. The payload is ~2.8MB across nine files, and the whole install is a
// couple of seconds at this size; a bigger buffer buys nothing measurable and a
// smaller one turns the SZS file into hundreds of round trips.
#define COPY_CHUNK (64 * 1024)

// One size for every path buffer in this file. The longest real path is about
// 80 characters; the headroom is for a deeper payload tree later, and path_join
// below turns "it did not fit" into an error rather than a wrong filename.
#define VP_PATH_MAX 512

static char s_copy_buffer[COPY_CHUNK];

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

// Joins two path parts and reports whether the result fit.
//
// The check is not there to satisfy -Wformat-truncation. A truncated path here
// would not be a cosmetic problem: it would name a *different* file, and the
// install would quietly write to the wrong place. Every caller treats a false
// return as a hard failure.
static bool path_join(char *out, size_t cap, const char *head, const char *tail)
{
    int n = snprintf(out, cap, "%s/%s", head, tail);
    return n > 0 && (size_t)n < cap;
}

static bool mod_dir(char *out, size_t cap)
{
    int n = snprintf(out, cap, "%s/%s/%s", HOTSWAP_ROOT, VP_GAME_SLUG, VP_MOD_SLUG);
    return n > 0 && (size_t)n < cap;
}

const char *vp_dest_path(char *out, size_t cap)
{
    char base[VP_PATH_MAX];
    if (!mod_dir(base, sizeof(base)) ||
        !path_join(out, cap, base, "layeredfs/romfs"))
    {
        if (cap) out[0] = '\0';
    }
    return out;
}

// mkdir -p. Walks the string creating each component, and treats "it already
// exists" as success -- which is the normal case for every component except the
// last on a reinstall.
static bool make_dirs(const char *path)
{
    char work[VP_PATH_MAX];
    snprintf(work, sizeof(work), "%s", path);

    // Skip the "sdmc:/" prefix: the drive itself is not a directory that can be
    // created, and asking for it fails.
    char *cursor = work;
    if (strncmp(cursor, SD_ROOT, strlen(SD_ROOT)) == 0) cursor += strlen(SD_ROOT);

    for (; *cursor; cursor++)
    {
        if (*cursor != '/') continue;

        *cursor = '\0';
        if (vp_mkdir(work) != 0 && errno != EEXIST) return false;
        *cursor = '/';
    }

    return vp_mkdir(work) == 0 || errno == EEXIST;
}

static bool path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

// ---------------------------------------------------------------------------
// Hotswap state
// ---------------------------------------------------------------------------

bool vp_mod_is_active(void)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s/state", HOTSWAP_ROOT, VP_GAME_SLUG);

    FILE *f = fopen(path, "rb");
    if (!f) return false;              // no state file: nothing has been swapped

    // Hotswap's state is three `key=value` lines and never grows, so a fixed
    // buffer that reads the whole thing is enough and avoids a line loop.
    char buf[256];
    size_t got = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[got] = '\0';

    const char *at = strstr(buf, "layeredfs=");
    if (!at) return false;             // pre-per-slot state file; nothing to match
    at += strlen("layeredfs=");

    size_t len = strcspn(at, "\r\n");
    return len == strlen(VP_MOD_SLUG) && strncmp(at, VP_MOD_SLUG, len) == 0;
}

bool vp_is_installed(void)
{
    char dest[VP_PATH_MAX];
    vp_dest_path(dest, sizeof(dest));
    return path_exists(dest);
}

// ---------------------------------------------------------------------------
// Walking the payload
// ---------------------------------------------------------------------------

// Recurses `src`, and for every regular file calls `visit`. `rel` is the path
// relative to VP_PAYLOAD_ROOT, which is what the destination is built from.
//
// Recursive rather than a hardcoded list of the nine files on purpose: the
// payload is whatever the build put in romfs/mod, so a track that later ships
// another file needs a rebuild and nothing else.
typedef bool (*visit_fn)(const char *src, const char *rel, void *user);

static bool walk(const char *src_dir, const char *rel_dir, visit_fn visit, void *user)
{
    DIR *dir = opendir(src_dir);
    if (!dir) return false;

    bool ok = true;
    struct dirent *entry;

    while (ok && (entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char src[VP_PATH_MAX], rel[VP_PATH_MAX];
        if (!path_join(src, sizeof(src), src_dir, entry->d_name)) { ok = false; break; }

        if (rel_dir[0])
        {
            if (!path_join(rel, sizeof(rel), rel_dir, entry->d_name)) { ok = false; break; }
        }
        else
        {
            int n = snprintf(rel, sizeof(rel), "%s", entry->d_name);
            if (n < 0 || (size_t)n >= sizeof(rel)) { ok = false; break; }
        }

        struct stat st;
        if (stat(src, &st) != 0) { ok = false; break; }

        if (S_ISDIR(st.st_mode))
            ok = walk(src, rel, visit, user);
        else
            ok = visit(src, rel, user);
    }

    closedir(dir);
    return ok;
}

typedef struct {
    int files;
    unsigned long long bytes;
} tally_t;

static bool tally_visit(const char *src, const char *rel, void *user)
{
    (void)rel;
    struct stat st;
    if (stat(src, &st) != 0) return false;

    tally_t *t = (tally_t *)user;
    t->files++;
    t->bytes += (unsigned long long)st.st_size;
    return true;
}

bool vp_payload_stat(int *files, unsigned long long *bytes)
{
    tally_t t = { 0, 0 };
    if (!walk(VP_PAYLOAD_ROOT, "", tally_visit, &t)) return false;

    if (files) *files = t.files;
    if (bytes) *bytes = t.bytes;
    return t.files > 0;
}

// ---------------------------------------------------------------------------
// The install
// ---------------------------------------------------------------------------

typedef struct {
    char dest_root[VP_PATH_MAX];
    vp_progress_fn progress;
    void *user;
    int done;
    int total;
    unsigned long long bytes;
    vp_result_t error;
} copy_ctx_t;

// Copies one file and then reads it back to confirm its size.
//
// The read-back is not paranoia. A short write to a tired SD card returns
// success from fwrite and only shows up much later as MK7 hanging on the track
// load screen, with nothing on the console to say why. Checking here turns a
// silent bad install into a message.
static bool copy_visit(const char *src, const char *rel, void *user)
{
    copy_ctx_t *ctx = (copy_ctx_t *)user;

    if (ctx->progress) ctx->progress(rel, ctx->done, ctx->total, ctx->user);

    char dest[VP_PATH_MAX];
    if (!path_join(dest, sizeof(dest), ctx->dest_root, rel))
    {
        ctx->error = VP_ERR_WRITE;
        return false;
    }

    // Everything up to the last slash is the directory this file needs.
    char dir[VP_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", dest);
    char *cut = strrchr(dir, '/');
    if (cut)
    {
        *cut = '\0';
        if (!make_dirs(dir)) { ctx->error = VP_ERR_MKDIR; return false; }
    }

    FILE *in = fopen(src, "rb");
    if (!in) { ctx->error = VP_ERR_READ; return false; }

    FILE *out = fopen(dest, "wb");
    if (!out) { fclose(in); ctx->error = VP_ERR_WRITE; return false; }

    unsigned long long written = 0;
    bool ok = true;

    for (;;)
    {
        size_t got = fread(s_copy_buffer, 1, sizeof(s_copy_buffer), in);
        if (got == 0) break;

        if (fwrite(s_copy_buffer, 1, got, out) != got)
        {
            ctx->error = VP_ERR_WRITE;
            ok = false;
            break;
        }
        written += got;
    }

    if (ok && ferror(in)) { ctx->error = VP_ERR_READ; ok = false; }

    fclose(in);
    // fclose is where a full card actually reports itself, because the last
    // buffer is flushed here rather than by the fwrite above.
    if (fclose(out) != 0 && ok) { ctx->error = VP_ERR_WRITE; ok = false; }
    if (!ok) return false;

    struct stat want, got_st;
    if (stat(src, &want) != 0 || stat(dest, &got_st) != 0 ||
        want.st_size != got_st.st_size)
    {
        ctx->error = VP_ERR_VERIFY;
        return false;
    }

    ctx->done++;
    ctx->bytes += written;
    return true;
}

vp_result_t vp_install(vp_progress_fn progress, void *user,
                       int *files_written, unsigned long long *bytes_written)
{
    if (files_written)  *files_written = 0;
    if (bytes_written)  *bytes_written = 0;

    // Refuse rather than fight Hotswap for the files. While the mod is swapped
    // in, its `layeredfs` folder has been renamed away into /luma/titles/, and
    // writing a new one here would leave Hotswap's state pointing at one copy
    // and the player looking at another. Telling them to swap back to stock
    // first is one sentence and cannot corrupt anything.
    if (vp_mod_is_active()) return VP_ERR_ACTIVE;

    copy_ctx_t ctx = { { 0 }, progress, user, 0, 0, 0, VP_OK };
    vp_dest_path(ctx.dest_root, sizeof(ctx.dest_root));

    if (!vp_payload_stat(&ctx.total, NULL)) return VP_ERR_READ;
    if (!make_dirs(ctx.dest_root))          return VP_ERR_MKDIR;

    if (!walk(VP_PAYLOAD_ROOT, "", copy_visit, &ctx))
        return ctx.error != VP_OK ? ctx.error : VP_ERR_READ;

    if (files_written)  *files_written = ctx.done;
    if (bytes_written)  *bytes_written = ctx.bytes;
    return VP_OK;
}

const char *vp_result_str(vp_result_t r)
{
    switch (r)
    {
        case VP_OK:         return "Installed.";
        case VP_ERR_ACTIVE: return "Verdant Pass is swapped in right now. Open Hotswap, "
                                   "switch Mario Kart 7 back to Stock, then run this again.";
        case VP_ERR_MKDIR:  return "Could not create the folder on the SD card.";
        case VP_ERR_READ:   return "Could not read the track out of this app.";
        case VP_ERR_WRITE:  return "Could not write to the SD card. It may be full or locked.";
        case VP_ERR_VERIFY: return "A file was written but came back the wrong size. "
                                   "The SD card may be failing.";
    }
    return "Unknown error.";
}
