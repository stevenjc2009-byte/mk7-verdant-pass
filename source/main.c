// Verdant Pass installer for Mario Kart 7.
//
// This app does one small thing: it writes a custom track onto the SD card in
// the layout Hotswap reads, so the track shows up in Hotswap's mod list for
// Mario Kart 7 and can be swapped in and out against stock or CTGP-7.
//
// It deliberately does NOT patch, launch or otherwise touch Mario Kart 7. It
// writes files into /hotswap/mk7/ and stops. Everything about actually putting
// those files in front of the game - the rename into /luma/titles/, the
// bookkeeping, swapping back to stock - is Hotswap's job and stays there.
//
// The front end is a plain text console rather than citro2d. There are three
// buttons and a progress line; a GPU front end would be more code than the
// install itself and nothing on screen would look better for it.

#include <3ds.h>

#include <stdio.h>
#include <string.h>

#include "install.h"
#include "updater.h"
#include "version.h"

// ANSI colours the 3DS console understands. Named rather than inlined so the
// screens stay readable.
#define C_RESET  "\x1b[0m"
#define C_TITLE  "\x1b[32;1m"   // green, for the track
#define C_DIM    "\x1b[37m"
#define C_WARN   "\x1b[33;1m"
#define C_ERR    "\x1b[31;1m"
#define C_OK     "\x1b[32;1m"
#define C_KEY    "\x1b[36;1m"

static PrintConsole s_top, s_bottom;

// What the top screen is currently saying. Redrawn whole on every change rather
// than scrolled, so a long error cannot push the header off the screen.
static char s_status[512];
static bool s_status_is_error;

// Counted once at boot so the top screen can say what is about to be written
// before anything is written.
static int                s_payload_files;
static unsigned long long s_payload_bytes;

static void set_status(const char *text, bool is_error)
{
    snprintf(s_status, sizeof(s_status), "%s", text ? text : "");
    s_status_is_error = is_error;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void draw_top(void)
{
    consoleSelect(&s_top);
    consoleClear();

    printf("\n  " C_TITLE "VERDANT PASS" C_RESET "\n");
    printf("  " C_DIM "A custom track for Mario Kart 7" C_RESET "\n\n");

    char dest[512];
    vp_dest_path(dest, sizeof(dest));

    printf("  version  " C_KEY "%s" C_RESET "\n",
           VP_VERSION_SET ? VP_VERSION : "(unset)");
    printf("  payload  %d files, %llu KB\n",
           s_payload_files, s_payload_bytes / 1024);
    printf("  goes to  " C_DIM "%s" C_RESET "\n", dest + strlen("sdmc:"));
    printf("  status   %s\n\n",
           vp_is_installed() ? C_OK "installed" C_RESET
                             : C_WARN "not installed yet" C_RESET);

    printf("  " C_DIM "----------------------------------------" C_RESET "\n\n");

    if (s_status[0])
        printf("  %s%s" C_RESET "\n", s_status_is_error ? C_ERR : "", s_status);
}

static void draw_bottom(void)
{
    consoleSelect(&s_bottom);
    consoleClear();

    bool busy = updaterBusy();

    printf("\n");
    if (busy)
    {
        // Nothing is offered while the worker owns the job. A cancelled
        // download leaves a half-written title behind, so there is no way out
        // on purpose.
        printf("  " C_DIM "Working. Please wait..." C_RESET "\n\n");

        int pct = updaterProgress();
        if (pct >= 0)
        {
            printf("  [");
            for (int i = 0; i < 24; i++) putchar(i < pct * 24 / 100 ? '#' : '.');
            printf("] %3d%%\n", pct);
        }
        return;
    }

    if (updaterState() == UPDATE_AVAILABLE)
    {
        printf("  " C_WARN "Version %s is available." C_RESET "\n\n",
               updaterLatestVersion());
        printf("    " C_KEY "A" C_RESET "      download and install it\n");
        printf("    " C_KEY "B" C_RESET "      not now\n\n");
        printf("  " C_DIM "The console will restart into the new\n"
               "  version, which then writes the new track." C_RESET "\n");
        return;
    }

    if (updaterState() == UPDATE_DONE)
    {
        printf("  " C_OK "Installed." C_RESET "\n\n");
        printf("    " C_KEY "A" C_RESET "      restart into the new version\n");
        return;
    }

    printf("    " C_KEY "A" C_RESET "      %s Verdant Pass\n",
           vp_is_installed() ? "reinstall" : "install");

    if (updaterAvailable())
        printf("    " C_KEY "Y" C_RESET "      check for updates\n");
    else
        printf("    " C_DIM "Y      check for updates (unavailable)" C_RESET "\n");

    printf("    " C_KEY "START" C_RESET "  exit\n\n");

    printf("  " C_DIM "Then open Hotswap, pick Mario Kart 7,\n"
           "  and choose Verdant Pass." C_RESET "\n");
}

static void redraw(void)
{
    draw_top();
    draw_bottom();
}

// ---------------------------------------------------------------------------
// The install
// ---------------------------------------------------------------------------

static void on_progress(const char *name, int done, int total, void *user)
{
    (void)user;

    char line[256];
    snprintf(line, sizeof(line), "Writing %d/%d  %s", done + 1, total, name);
    set_status(line, false);

    // Painted straight away rather than at the end of the frame: the copy is a
    // blocking loop, so this is the only chance to show anything at all.
    draw_top();
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

static void run_install(void)
{
    set_status("Starting...", false);
    redraw();

    int files = 0;
    unsigned long long bytes = 0;
    vp_result_t r = vp_install(on_progress, NULL, &files, &bytes);

    if (r == VP_OK)
    {
        char line[256];
        snprintf(line, sizeof(line),
                 "Installed. %d files, %llu KB written.\n\n"
                 "  Open Hotswap and pick Verdant Pass\n"
                 "  under Mario Kart 7.",
                 files, bytes / 1024);
        set_status(line, false);
    }
    else
    {
        set_status(vp_result_str(r), true);
    }
}

// ---------------------------------------------------------------------------

int main(void)
{
    gfxInitDefault();

    consoleInit(GFX_TOP, &s_top);
    consoleInit(GFX_BOTTOM, &s_bottom);

    // RomFs is mounted here and owned here. Both the track payload and the
    // updater's certificate bundle live in it, so it comes up before either and
    // goes down after both.
    bool romfs_up = R_SUCCEEDED(romfsInit());

    if (!romfs_up)
    {
        set_status("This build has no track payload attached, so there is "
                   "nothing to install.", true);
    }
    else if (!vp_payload_stat(&s_payload_files, &s_payload_bytes))
    {
        set_status("The track payload could not be read out of this app.", true);
        romfs_up = false;
    }
    else if (vp_mod_is_active())
    {
        // Said up front rather than only on a failed install: the player should
        // know before pressing anything that the files are in use.
        set_status(vp_result_str(VP_ERR_ACTIVE), true);
    }

    // Only meaningful on a CIA build - a bare .3dsx has no AM service - and the
    // update button greys itself out when this fails.
    updaterInit();

    redraw();

    while (aptMainLoop())
    {
        hidScanInput();
        u32 down = hidKeysDown();

        bool busy = updaterBusy();
        updateState before = updaterState();

        if (!busy)
        {
            if (before == UPDATE_DONE)
            {
                if (down & KEY_A)
                {
                    updaterRelaunch();
                    break;
                }
            }
            else if (before == UPDATE_AVAILABLE)
            {
                if (down & KEY_A) updaterStartInstall();
                if (down & KEY_B) { set_status("Update skipped.", false); redraw(); }
            }
            else
            {
                if ((down & KEY_A) && romfs_up)
                {
                    run_install();
                    redraw();
                }
                if ((down & KEY_Y) && updaterAvailable())
                {
                    updaterStartCheck();
                }
                if (down & KEY_START) break;
            }
        }

        // The worker moves the state from another thread, so the screens are
        // repainted whenever it changes as well as on a button press.
        if (updaterState() != before || down)
        {
            if (updaterState() != before)
            {
                updateState now = updaterState();
                if (now == UPDATE_FAILED)
                    set_status(updaterMessage(), true);
                else if (now != UPDATE_IDLE)
                    set_status(updaterMessage(), false);
            }
            redraw();
        }
        else if (updaterBusy())
        {
            // Repaint anyway while a download is running, so the bar moves.
            draw_bottom();
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    updaterExit();
    if (romfs_up) romfsExit();
    gfxExit();
    return 0;
}
