// The front end.
//
// citro2d rather than the text console this app started with. Two reasons that
// are not decoration: a GPU front end repaints every frame, so a state change
// made by the updater's worker thread between frames shows up on the next one
// without anything having to notice it - the console version only repainted on
// a button press, and a check that finished quietly sat on "please wait" until
// the screen was tapped. And the bottom screen becomes real touchable buttons
// instead of a printed list of letters.
//
// Everything about *which* buttons exist and where they are lives in uimodel.h,
// which has no GPU in it and is covered by the host tests. This file only draws.

#pragma once

#include <stdbool.h>

#include "uimodel.h"

// What the top screen says. Filled fresh by the caller each frame.
typedef struct {
    const char *version;             ///< This build's version, or NULL if unset.
    unsigned long long track_bytes;  ///< Size of the data this app carries.
    int  file_count;                 ///< How many files the install writes.
    const char *dest;                ///< Where they go, for the player to see.
    bool installed;                  ///< Already on the card.

    const char *message;             ///< The status line. May be NULL.
    bool message_is_error;

    int progress;                    ///< 0-100, or -1 for no bar.
    const char *busy_label;          ///< Shown with the bar. May be NULL.

    const char *latest_version;      ///< Set once a check finds a newer release.
} ui_screen;

// Brings up C3D/C2D and the two render targets. False if the GPU refused, in
// which case there is nothing to fall back to and the app should bail out.
bool ui_init(void);
void ui_exit(void);

// Draws one frame of both screens. `selection` is the highlighted button, or -1.
void ui_frame(const ui_screen *screen, const ui_context *ctx, int selection);
