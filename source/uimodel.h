// What the front end offers, and what pressing it means.
//
// Split out of the drawing code deliberately. The buttons on the bottom screen
// change with what the app is doing - mid-install there are none, after a check
// finds a newer release there are two different ones - and getting that wrong is
// how a player ends up able to start a second install on top of a running one.
// None of it needs a GPU to decide, so none of it is in ui.c, and all of it is
// covered by the host tests.
//
// The drawing in ui.c asks this what to put on screen. It never decides for
// itself, so a button that is not in this list cannot be pressed.

#pragma once

#include <stdbool.h>

#include "updatestate.h"

typedef enum {
    UI_ACT_NONE = 0,
    UI_ACT_INSTALL,          ///< Build the track onto the SD card.
    UI_ACT_CHECK,            ///< Ask GitHub whether there is a newer release.
    UI_ACT_EXIT,
    UI_ACT_UPDATE_INSTALL,   ///< Download and install the newer release.
    UI_ACT_UPDATE_SKIP,      ///< Not now.
    UI_ACT_RELAUNCH,         ///< Restart into the version just installed.
} ui_action;

// Everything the button list depends on. Filled from install.h and updater.h by
// the caller each frame; this header knows about neither.
typedef struct {
    updateState update;
    bool busy;             ///< The updater's worker thread owns the app.
    bool installed;        ///< The track is already on the card.
    bool payload_ok;       ///< This build has its track data attached.
    bool updater_ready;    ///< Sockets and AM came up (false on a .3dsx build).
} ui_context;

#define UI_MAX_BUTTONS 3

typedef struct {
    ui_action   action;
    const char *label;
    const char *hint;      ///< One line under the label. Never NULL.
    bool        enabled;   ///< Drawn greyed and does nothing when false.
} ui_button;

// Fills `out` with the buttons for this state and returns how many.
//
// Returns 0 while the updater is busy: there is deliberately no way to cancel,
// because an interrupted download leaves a half-written title behind.
int ui_buttons(const ui_context *ctx, ui_button out[UI_MAX_BUTTONS]);

// The action for a button index, or UI_ACT_NONE if the index is out of range or
// the button is disabled. Every input path - touch and buttons alike - goes
// through this, so a disabled button cannot be triggered by either.
ui_action ui_activate(const ui_context *ctx, int index);

// Moves the highlight, skipping disabled buttons and stopping at the ends
// rather than wrapping. Returns the new index; -1 if there is nothing to
// select. `delta` is -1 for up and +1 for down.
int ui_move_selection(const ui_context *ctx, int index, int delta);

// What to say while the worker owns the app. Named per state rather than a
// single "working", because the check takes seconds and the download takes
// most of a minute, and one label for both is indistinguishable from a hang.
const char *ui_busy_label(updateState state);

// --- Where the buttons are --------------------------------------------------
//
// On the bottom screen, which is 320x240. The geometry lives here rather than in
// ui.c because the touch handler and the drawing have to agree exactly: a rect
// that is drawn in one place and tested in another is a button that misses, and
// on a touch screen that reads as the app ignoring the player.

#define UI_SCREEN_W  320.0f
#define UI_SCREEN_H  240.0f
#define UI_BTN_X      16.0f
#define UI_BTN_TOP    52.0f
#define UI_BTN_W     288.0f
#define UI_BTN_H      50.0f
#define UI_BTN_GAP     8.0f

void ui_button_rect(int index, float *x, float *y, float *w, float *h);

// Which button a touch landed on, or -1 for none, a disabled one, or a touch
// while the updater is busy.
int ui_hit_test(const ui_context *ctx, float touch_x, float touch_y);
