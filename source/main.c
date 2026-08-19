// Verdant Pass installer for Mario Kart 7.
//
// This app builds a custom track onto the SD card in the layout Hotswap reads,
// so the track shows up in Hotswap's mod list for Mario Kart 7 and can be
// swapped in and out against stock or CTGP-7.
//
// It ships none of Nintendo's data. What it carries is the three files this
// project generated - the model, the collision mesh and the course layout - and
// it reads everything else out of the player's own copy of the game, assembling
// the nine finished files here on the console. See patch.h for the recipe.
//
// It deliberately does NOT patch, launch or otherwise touch Mario Kart 7. It
// writes files into /hotswap/mk7/ and stops. Everything about actually putting
// those files in front of the game - the rename into /luma/titles/, the
// bookkeeping, swapping back to stock - is Hotswap's job and stays there.
//
// The loop below is a plain 60 Hz repaint. That is not laziness: the updater
// runs on a worker thread, and a front end that only repaints when something
// noticed a change is one missed edge away from sitting on "please wait"
// forever. Painting every frame cannot miss an edge.

#include <3ds.h>

#include <stdio.h>
#include <string.h>

#include "install.h"
#include "ui.h"
#include "uimodel.h"
#include "updater.h"
#include "version.h"

// What the top screen is saying. Set from one place so a message can never be
// half replaced.
static char s_message[512];
static bool s_message_is_error;

static void set_message(const char *text, bool is_error)
{
    snprintf(s_message, sizeof(s_message), "%s", text ? text : "");
    s_message_is_error = is_error;
}

// Everything the screens read. Rebuilt each frame from the two engines rather
// than kept in step by hand.
static ui_screen        s_screen;
static ui_context       s_ctx;
static char             s_dest[512];
static unsigned long long s_track_bytes;
static bool             s_payload_ok;

static void refresh(void)
{
    s_ctx.update        = updaterState();
    s_ctx.busy          = updaterBusy();
    s_ctx.installed     = vp_is_installed();
    s_ctx.payload_ok    = s_payload_ok;
    s_ctx.updater_ready = updaterAvailable();

    s_screen.version         = VP_VERSION_SET ? VP_VERSION : NULL;
    s_screen.track_bytes     = s_track_bytes;
    s_screen.file_count      = vp_step_count();
    s_screen.dest            = s_dest;
    s_screen.installed       = s_ctx.installed;
    s_screen.message         = s_message[0] ? s_message : NULL;
    s_screen.message_is_error = s_message_is_error;
    s_screen.latest_version  = updaterLatestVersion();

    if (s_ctx.busy)
    {
        s_screen.busy_label = ui_busy_label(s_ctx.update);
        s_screen.progress   = updaterProgress();
    }
    else
    {
        s_screen.busy_label = NULL;
        s_screen.progress   = -1;
    }
}

// ---------------------------------------------------------------------------
// The install
// ---------------------------------------------------------------------------

// The install is a blocking loop - decompressing and recompressing a three
// megabyte archive is not something to thread on this hardware - so the frame
// is driven from inside it. Without this the screen would sit still for the
// whole job, which is the same failure the updater used to have.
static void on_progress(const char *name, int done, int total, void *user)
{
    (void)user;

    char line[256];
    snprintf(line, sizeof(line), "Building %d of %d", done + 1, total);
    set_message(name, false);

    refresh();
    s_screen.busy_label = line;
    s_screen.progress   = total > 0 ? done * 100 / total : -1;

    // The context says busy so the bottom screen shows the wait page and offers
    // nothing. The real updaterBusy() is false here - this is our own blocking
    // work, not the worker thread's.
    ui_context busy = s_ctx;
    busy.busy = true;

    ui_frame(&s_screen, &busy, -1);
}

static void run_install(void)
{
    set_message("Reading Mario Kart 7...", false);

    int files = 0;
    unsigned long long bytes = 0;
    vp_result_t r = vp_install(on_progress, NULL, &files, &bytes);

    if (r == VP_OK)
    {
        char line[256];
        snprintf(line, sizeof(line),
                 "Done. %d files, %llu KB written.\n"
                 "Open Hotswap, pick Mario Kart 7, and choose Verdant Pass.",
                 files, bytes / 1024);
        set_message(line, false);
    }
    else
    {
        set_message(vp_result_str(r), true);
    }
}

// ---------------------------------------------------------------------------

int main(void)
{
    gfxInitDefault();

    if (!ui_init())
    {
        gfxExit();
        return 1;
    }

    // RomFs is mounted here and owned here. Both our three track files and the
    // updater's certificate bundle live in it, so it comes up before either and
    // goes down after both.
    bool romfs_up = R_SUCCEEDED(romfsInit());

    s_payload_ok = romfs_up && vp_payload_ok(&s_track_bytes);
    vp_dest_path(s_dest, sizeof(s_dest));

    if (!romfs_up)
        set_message("This build has no track data attached, so there is nothing "
                    "to install.", true);
    else if (!s_payload_ok)
        set_message("The track data could not be read out of this app.", true);
    else if (vp_mod_is_active())
        // Said up front rather than only on a failed install: the player should
        // know before pressing anything that Hotswap has the files.
        set_message(vp_result_str(VP_ERR_ACTIVE), true);

    // Only meaningful on a CIA build - a bare .3dsx has no AM service - and the
    // update button greys itself out when this fails.
    updaterInit();

    int selection = 0;
    updateState seen = updaterState();

    while (aptMainLoop())
    {
        hidScanInput();
        u32 down = hidKeysDown();

        refresh();

        // The worker thread moves the state between frames. Noticing it here is
        // only for putting its message on screen - the repaint happens anyway,
        // every frame, so a missed edge costs nothing.
        if (s_ctx.update != seen)
        {
            seen = s_ctx.update;
            if (seen == UPDATE_FAILED)     set_message(updaterMessage(), true);
            else if (seen != UPDATE_IDLE)  set_message(updaterMessage(), false);

            selection = ui_move_selection(&s_ctx, selection, 0);
            if (selection < 0) selection = 0;
        }

        ui_action action = UI_ACT_NONE;

        if (!s_ctx.busy)
        {
            if (down & KEY_DOWN)  selection = ui_move_selection(&s_ctx, selection, 1);
            if (down & KEY_UP)    selection = ui_move_selection(&s_ctx, selection, -1);
            if (down & KEY_A)     action = ui_activate(&s_ctx, selection);
            if (down & KEY_B)     action = ui_activate(&s_ctx, 1);   // "not now"

            if (down & KEY_TOUCH)
            {
                touchPosition touch;
                hidTouchRead(&touch);

                int hit = ui_hit_test(&s_ctx, (float)touch.px, (float)touch.py);
                if (hit >= 0)
                {
                    selection = hit;
                    action = ui_activate(&s_ctx, hit);
                }
            }

            // START always exits, except once an update is installed: leaving
            // then would strand the player on the old build with the new one
            // already committed.
            if ((down & KEY_START) && s_ctx.update != UPDATE_DONE)
                action = UI_ACT_EXIT;
        }

        switch (action)
        {
            case UI_ACT_INSTALL:
                run_install();
                break;

            case UI_ACT_CHECK:
                updaterStartCheck();
                set_message(updaterMessage(), false);
                seen = updaterState();
                break;

            case UI_ACT_UPDATE_INSTALL:
                updaterStartInstall();
                seen = updaterState();
                break;

            case UI_ACT_UPDATE_SKIP:
                set_message("Update skipped.", false);
                break;

            case UI_ACT_RELAUNCH:
                updaterRelaunch();
                goto done;

            case UI_ACT_EXIT:
                goto done;

            case UI_ACT_NONE:
                break;
        }

        refresh();
        ui_frame(&s_screen, &s_ctx, selection);
    }

done:
    updaterExit();
    if (romfs_up) romfsExit();
    ui_exit();
    gfxExit();
    return 0;
}
