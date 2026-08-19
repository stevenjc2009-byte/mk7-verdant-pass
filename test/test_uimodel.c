// Host test for the front end's button logic.
//
// The drawing needs a console, but what is on the screen and what pressing it
// does is plain C and is decided here. The cases that matter are the ones a
// player only hits when something is already going wrong: a build with no track
// data, a .3dsx with no update service, and above all a running download, where
// offering any button at all would let them start a second install on top of a
// half-written title.
//
// Built and run by test/run.sh.

#include <stdio.h>
#include <string.h>

#include "uimodel.h"

static int s_checks, s_failures;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok) { s_failures++; printf("  FAIL  %s\n", what); }
}

// A healthy CIA build sitting idle: track data attached, updater up.
static ui_context healthy(void)
{
    ui_context c = { UPDATE_IDLE, false, false, true, true };
    return c;
}

static int find(const ui_context *ctx, ui_action action)
{
    ui_button b[UI_MAX_BUTTONS];
    int n = ui_buttons(ctx, b);
    for (int i = 0; i < n; i++) if (b[i].action == action) return i;
    return -1;
}

int main(void)
{
    printf("ui model\n");

    ui_button b[UI_MAX_BUTTONS];

    // --- the ordinary screen -------------------------------------------------

    ui_context ctx = healthy();
    check("idle offers install, update check and exit", ui_buttons(&ctx, b) == 3);
    check("install comes first", b[0].action == UI_ACT_INSTALL);
    check("install is offered", b[0].enabled);
    check("says install when nothing is on the card",
          strcmp(b[0].label, "Install track") == 0);

    ctx.installed = true;
    ui_buttons(&ctx, b);
    check("says reinstall once it is on the card",
          strcmp(b[0].label, "Reinstall track") == 0);

    // --- degraded builds -----------------------------------------------------

    ctx = healthy();
    ctx.payload_ok = false;
    ui_buttons(&ctx, b);
    check("a build with no track data still shows the button", b[0].action == UI_ACT_INSTALL);
    check("...but greyed out", !b[0].enabled);
    check("...and pressing it does nothing", ui_activate(&ctx, 0) == UI_ACT_NONE);
    check("...and says why", strstr(b[0].hint, "no track data") != NULL);

    ctx = healthy();
    ctx.updater_ready = false;
    ui_buttons(&ctx, b);
    check("no updater means the check is greyed out", !b[1].enabled);
    check("...and pressing it does nothing", ui_activate(&ctx, 1) == UI_ACT_NONE);
    check("...but install still works", ui_activate(&ctx, 0) == UI_ACT_INSTALL);

    // --- while the worker owns the app ---------------------------------------

    // The one that matters most. Every busy state offers nothing, including the
    // two that are easy to forget: the version check, which is short enough to
    // feel safe, and the AM commit, where a second install would be worst.
    const updateState BUSY_STATES[] = {
        UPDATE_CHECKING, UPDATE_DOWNLOADING, UPDATE_INSTALLING
    };
    for (int i = 0; i < 3; i++)
    {
        ctx = healthy();
        ctx.update = BUSY_STATES[i];
        ctx.busy = true;

        char label[128];
        snprintf(label, sizeof(label), "busy (%d) offers nothing", (int)BUSY_STATES[i]);
        check(label, ui_buttons(&ctx, b) == 0);

        snprintf(label, sizeof(label), "busy (%d) ignores a press", (int)BUSY_STATES[i]);
        check(label, ui_activate(&ctx, 0) == UI_ACT_NONE);

        snprintf(label, sizeof(label), "busy (%d) has nothing to select", (int)BUSY_STATES[i]);
        check(label, ui_move_selection(&ctx, 0, 1) == -1);

        snprintf(label, sizeof(label), "busy (%d) says what it is doing", (int)BUSY_STATES[i]);
        check(label, strcmp(ui_busy_label(BUSY_STATES[i]), "Working") != 0);
    }

    // The three busy states must not all read the same, or the screen cannot be
    // told apart from a hung one.
    check("each busy state reads differently",
          strcmp(ui_busy_label(UPDATE_CHECKING), ui_busy_label(UPDATE_DOWNLOADING)) != 0 &&
          strcmp(ui_busy_label(UPDATE_DOWNLOADING), ui_busy_label(UPDATE_INSTALLING)) != 0);

    // --- an update was found -------------------------------------------------

    ctx = healthy();
    ctx.update = UPDATE_AVAILABLE;
    check("an available update offers two choices", ui_buttons(&ctx, b) == 2);
    check("update first", ui_activate(&ctx, 0) == UI_ACT_UPDATE_INSTALL);
    check("skip second", ui_activate(&ctx, 1) == UI_ACT_UPDATE_SKIP);
    check("and nothing else", ui_activate(&ctx, 2) == UI_ACT_NONE);
    check("installing the track is not offered mid-decision",
          find(&ctx, UI_ACT_INSTALL) < 0);

    // A highlight left on the third button by the previous screen must not
    // survive into a two-button one and fire nothing.
    check("a stale highlight is clamped, not dropped",
          ui_move_selection(&ctx, 2, -1) == 0);

    // --- the update went in --------------------------------------------------

    ctx = healthy();
    ctx.update = UPDATE_DONE;
    check("a finished update offers one thing", ui_buttons(&ctx, b) == 1);
    check("and it is the restart", ui_activate(&ctx, 0) == UI_ACT_RELAUNCH);
    check("exit is not offered - leaving now strands the new build",
          find(&ctx, UI_ACT_EXIT) < 0);

    // --- a settled check ------------------------------------------------------

    ctx = healthy();
    ctx.update = UPDATE_UP_TO_DATE;
    check("up to date goes back to the ordinary screen", ui_buttons(&ctx, b) == 3);

    ctx.update = UPDATE_FAILED;
    check("a failed check goes back to the ordinary screen", ui_buttons(&ctx, b) == 3);
    check("...and lets it be tried again", ui_activate(&ctx, 1) == UI_ACT_CHECK);

    // --- moving the highlight -------------------------------------------------

    ctx = healthy();
    check("down moves on", ui_move_selection(&ctx, 0, 1) == 1);
    check("up moves back", ui_move_selection(&ctx, 1, -1) == 0);
    check("up from the top stays put", ui_move_selection(&ctx, 0, -1) == 0);
    check("down from the bottom stays put", ui_move_selection(&ctx, 2, 1) == 2);

    // A disabled button is stepped over rather than landed on, so the highlight
    // is never sitting on something that does nothing.
    ctx.updater_ready = false;
    check("a disabled button is skipped", ui_move_selection(&ctx, 0, 1) == 2);
    check("and skipped going back too", ui_move_selection(&ctx, 2, -1) == 0);

    // --- touch ----------------------------------------------------------------

    ctx = healthy();
    for (int i = 0; i < 3; i++)
    {
        float x, y, w, h;
        ui_button_rect(i, &x, &y, &w, &h);

        char label[64];
        snprintf(label, sizeof(label), "a tap in the middle of button %d hits it", i);
        check(label, ui_hit_test(&ctx, x + w / 2, y + h / 2) == i);

        // The edges matter: a button whose bottom row belongs to the next one
        // fires the wrong action on a tap the player aimed correctly.
        snprintf(label, sizeof(label), "button %d owns its top-left corner", i);
        check(label, ui_hit_test(&ctx, x, y) == i);

        snprintf(label, sizeof(label), "button %d does not own the row past its bottom", i);
        check(label, ui_hit_test(&ctx, x + w / 2, y + h) != i);
    }

    check("the gap between buttons hits nothing",
          ui_hit_test(&ctx, UI_BTN_X + 10, UI_BTN_TOP + UI_BTN_H + UI_BTN_GAP / 2) == -1);
    check("a tap left of the buttons hits nothing",
          ui_hit_test(&ctx, UI_BTN_X - 1, UI_BTN_TOP + 10) == -1);
    check("a tap right of the buttons hits nothing",
          ui_hit_test(&ctx, UI_BTN_X + UI_BTN_W, UI_BTN_TOP + 10) == -1);
    check("a tap above the buttons hits nothing",
          ui_hit_test(&ctx, UI_BTN_X + 10, UI_BTN_TOP - 1) == -1);
    check("the buttons fit on the screen",
          UI_BTN_TOP + 3 * UI_BTN_H + 2 * UI_BTN_GAP <= UI_SCREEN_H &&
          UI_BTN_X + UI_BTN_W <= UI_SCREEN_W);

    // Tapping a greyed-out button must do nothing, not fall through to whatever
    // is underneath it.
    ctx.updater_ready = false;
    {
        float x, y, w, h;
        ui_button_rect(1, &x, &y, &w, &h);
        check("tapping a greyed-out button does nothing",
              ui_hit_test(&ctx, x + w / 2, y + h / 2) == -1);
    }

    // And the whole screen is dead while a download is running. This is the tap
    // that used to bring the old console front end back to its menu mid-check.
    ctx = healthy();
    ctx.update = UPDATE_DOWNLOADING;
    ctx.busy = true;
    for (int i = 0; i < 3; i++)
    {
        float x, y, w, h;
        ui_button_rect(i, &x, &y, &w, &h);
        char label[64];
        snprintf(label, sizeof(label), "a tap on button %d is ignored while busy", i);
        check(label, ui_hit_test(&ctx, x + w / 2, y + h / 2) == -1);
    }

    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
