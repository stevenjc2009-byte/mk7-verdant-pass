#include "uimodel.h"

int ui_buttons(const ui_context *ctx, ui_button out[UI_MAX_BUTTONS])
{
    // No way out while the worker thread owns the job. See the header.
    if (ctx->busy) return 0;

    if (ctx->update == UPDATE_AVAILABLE)
    {
        out[0] = (ui_button){ UI_ACT_UPDATE_INSTALL, "Update",
                              "download and install the new version", true };
        out[1] = (ui_button){ UI_ACT_UPDATE_SKIP, "Not now",
                              "carry on with this version", true };
        return 2;
    }

    if (ctx->update == UPDATE_DONE)
    {
        out[0] = (ui_button){ UI_ACT_RELAUNCH, "Restart",
                              "start the version just installed", true };
        return 1;
    }

    out[0] = (ui_button){ UI_ACT_INSTALL,
                          ctx->installed ? "Reinstall track" : "Install track",
                          ctx->payload_ok ? "build it from your copy of Mario Kart 7"
                                          : "this build has no track data attached",
                          ctx->payload_ok };

    out[1] = (ui_button){ UI_ACT_CHECK, "Check for updates",
                          ctx->updater_ready ? "ask GitHub for a newer version"
                                             : "not available in this build",
                          ctx->updater_ready };

    out[2] = (ui_button){ UI_ACT_EXIT, "Exit", "close the installer", true };
    return 3;
}

ui_action ui_activate(const ui_context *ctx, int index)
{
    ui_button buttons[UI_MAX_BUTTONS];
    int count = ui_buttons(ctx, buttons);

    if (index < 0 || index >= count) return UI_ACT_NONE;
    if (!buttons[index].enabled)     return UI_ACT_NONE;

    return buttons[index].action;
}

int ui_move_selection(const ui_context *ctx, int index, int delta)
{
    ui_button buttons[UI_MAX_BUTTONS];
    int count = ui_buttons(ctx, buttons);
    if (count == 0) return -1;

    // Clamp an index that is stale from a previous state before moving it: the
    // button list changes underneath the highlight, and a check that finds an
    // update shrinks a list of three down to two.
    if (index < 0)      index = 0;
    if (index >= count) index = count - 1;

    for (int i = index + delta; i >= 0 && i < count; i += delta)
        if (buttons[i].enabled) return i;

    return index;   // nothing selectable that way; stay put rather than wrap
}

void ui_button_rect(int index, float *x, float *y, float *w, float *h)
{
    *x = UI_BTN_X;
    *y = UI_BTN_TOP + (float)index * (UI_BTN_H + UI_BTN_GAP);
    *w = UI_BTN_W;
    *h = UI_BTN_H;
}

int ui_hit_test(const ui_context *ctx, float touch_x, float touch_y)
{
    ui_button buttons[UI_MAX_BUTTONS];
    int count = ui_buttons(ctx, buttons);

    for (int i = 0; i < count; i++)
    {
        if (!buttons[i].enabled) continue;

        float x, y, w, h;
        ui_button_rect(i, &x, &y, &w, &h);

        if (touch_x >= x && touch_x < x + w && touch_y >= y && touch_y < y + h)
            return i;
    }
    return -1;
}

const char *ui_busy_label(updateState state)
{
    switch (state)
    {
        case UPDATE_CHECKING:    return "Asking GitHub for the newest version";
        case UPDATE_DOWNLOADING: return "Downloading the update";
        case UPDATE_INSTALLING:  return "Installing the update";
        default:                 return "Working";
    }
}
