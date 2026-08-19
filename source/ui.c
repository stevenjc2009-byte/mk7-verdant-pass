#include "ui.h"

#include <3ds.h>
#include <citro2d.h>

#include <stdio.h>
#include <string.h>

#define TOP_W   400.0f
#define SCR_H   240.0f

// A woodland palette, because the track is one. Kept as named constants rather
// than literals so the two screens cannot drift apart.
#define CLR_CANOPY   C2D_Color32(0x0C, 0x24, 0x18, 0xFF)   // deepest shade
#define CLR_FOREST   C2D_Color32(0x18, 0x42, 0x2B, 0xFF)
#define CLR_MOSS     C2D_Color32(0x22, 0x5A, 0x3A, 0xFF)
#define CLR_LEAF     C2D_Color32(0x7C, 0xD4, 0x8A, 0xFF)   // the accent
#define CLR_SUN      C2D_Color32(0xF4, 0xD9, 0x7A, 0xFF)
#define CLR_TEXT     C2D_Color32(0xEE, 0xF6, 0xF0, 0xFF)
#define CLR_MUTED    C2D_Color32(0x9D, 0xBA, 0xA8, 0xFF)
#define CLR_WARN     C2D_Color32(0xF2, 0xC1, 0x4E, 0xFF)
#define CLR_ERROR    C2D_Color32(0xEE, 0x8A, 0x7A, 0xFF)
#define CLR_PANEL    C2D_Color32(0xFF, 0xFF, 0xFF, 0x12)
#define CLR_PANEL_HI C2D_Color32(0xFF, 0xFF, 0xFF, 0x22)
#define CLR_TRACK    C2D_Color32(0x00, 0x00, 0x00, 0x55)

static C3D_RenderTarget *s_top, *s_bottom;
static C2D_TextBuf s_text;
static unsigned s_frame;

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

// One buffer, cleared once a frame. Every string drawn is re-parsed rather than
// cached: at these sizes it is nothing, and a cache would have to be invalidated
// every time a status line changed, which is most of what this screen does.
static void text_size(const char *s, float scale, float *w, float *h)
{
    C2D_Text t;
    C2D_TextParse(&t, s_text, s);
    C2D_TextGetDimensions(&t, scale, scale, w, h);
}

static void draw_text(const char *s, float x, float y, float scale, u32 colour)
{
    C2D_Text t;
    C2D_TextParse(&t, s_text, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, colour);
}

static void draw_text_right(const char *s, float right, float y, float scale, u32 colour)
{
    float w, h;
    text_size(s, scale, &w, &h);
    draw_text(s, right - w, y, scale, colour);
}

// Greedy word wrap. Returns the y below the last line drawn.
//
// Needed because the status line is a real sentence written for the player -
// "Mario Kart 7 was not found on this console" and worse - and a message that
// runs off the edge of the screen is the same as no message at all.
static float draw_wrapped(const char *s, float x, float y, float max_w,
                          float scale, float line_h, u32 colour)
{
    char line[256];
    size_t len = 0;

    while (*s)
    {
        // Take the next word, plus the space in front of it if there is one.
        const char *word = s;
        while (*s == ' ') s++;
        while (*s && *s != ' ' && *s != '\n') s++;

        size_t chunk = (size_t)(s - word);
        if (chunk == 0) { if (*s == '\n') s++; continue; }
        if (len + chunk >= sizeof(line)) break;

        memcpy(line + len, word, chunk);
        line[len + chunk] = '\0';

        float w, h;
        text_size(line, scale, &w, &h);

        if (w > max_w && len > 0)
        {
            // Too wide with this word: draw what fit, start again with the word.
            line[len] = '\0';
            draw_text(line, x, y, scale, colour);
            y += line_h;

            while (*word == ' ') word++;
            chunk = (size_t)(s - word);
            memcpy(line, word, chunk);
            line[chunk] = '\0';
            len = chunk;
        }
        else
        {
            len += chunk;
        }

        // An explicit newline in the message is honoured, so a multi-line
        // instruction stays laid out the way it was written.
        if (*s == '\n')
        {
            line[len] = '\0';
            draw_text(line, x, y, scale, colour);
            y += line_h;
            len = 0;
            s++;
        }
    }

    if (len)
    {
        line[len] = '\0';
        draw_text(line, x, y, scale, colour);
        y += line_h;
    }
    return y;
}

// ---------------------------------------------------------------------------
// Pieces
// ---------------------------------------------------------------------------

static void draw_panel(float x, float y, float w, float h, u32 colour)
{
    C2D_DrawRectSolid(x, y, 0.0f, w, h, colour);
}

// A bar with a filled portion. `pct` below zero draws a sweep instead - there is
// no byte count during the version check, and a bar frozen at zero looks broken.
static void draw_progress(float x, float y, float w, int pct)
{
    const float H = 8.0f;
    C2D_DrawRectSolid(x, y, 0.0f, w, H, CLR_TRACK);

    if (pct >= 0)
    {
        if (pct > 100) pct = 100;
        C2D_DrawRectSolid(x, y, 0.1f, w * (float)pct / 100.0f, H, CLR_LEAF);
        return;
    }

    // Indeterminate: a block that runs the length of the bar and wraps. Its only
    // job is to be visibly moving.
    const float BLOCK = w * 0.28f;
    float travel = (float)(s_frame % 120) / 120.0f * (w + BLOCK) - BLOCK;
    float from = travel < 0.0f ? 0.0f : travel;
    float to   = travel + BLOCK;
    if (to > w) to = w;
    if (to > from) C2D_DrawRectSolid(x + from, y, 0.1f, to - from, H, CLR_LEAF);
}

// The banner strip at the top of a screen: a darker band with a leaf-coloured
// rule under it, so both screens read as parts of the same app.
static void draw_header(float w, const char *title, const char *subtitle)
{
    C2D_DrawRectangle(0, 0, 0.0f, w, 46.0f, CLR_CANOPY, CLR_CANOPY, CLR_FOREST, CLR_FOREST);
    C2D_DrawRectSolid(0, 46.0f, 0.0f, w, 2.0f, CLR_LEAF);

    draw_text(title, 14.0f, 8.0f, 0.72f, CLR_TEXT);
    if (subtitle) draw_text(subtitle, 14.0f, 28.0f, 0.44f, CLR_MUTED);
}

// ---------------------------------------------------------------------------
// The screens
// ---------------------------------------------------------------------------

static void draw_row(const char *key, const char *value, float y, u32 value_colour)
{
    draw_text(key, 18.0f, y, 0.46f, CLR_MUTED);
    draw_text_right(value, TOP_W - 18.0f, y, 0.46f, value_colour);
}

static void draw_top_screen(const ui_screen *s, const ui_context *ctx)
{
    C2D_TargetClear(s_top, CLR_FOREST);
    C2D_SceneBegin(s_top);

    C2D_DrawRectangle(0, 0, 0.0f, TOP_W, SCR_H,
                      CLR_FOREST, CLR_FOREST, CLR_CANOPY, CLR_CANOPY);

    draw_header(TOP_W, "VERDANT PASS", "A custom track for Mario Kart 7");

    // The facts panel. Everything the player might want before pressing
    // anything: what version this is, how big the track is, where it goes.
    draw_panel(12.0f, 58.0f, TOP_W - 24.0f, 74.0f, CLR_PANEL);

    char buf[512];

    draw_row("version", s->version ? s->version : "not set", 64.0f,
             s->version ? CLR_TEXT : CLR_WARN);

    snprintf(buf, sizeof(buf), "%llu KB into %d files",
             s->track_bytes / 1024, s->file_count);
    draw_row("track", buf, 82.0f, CLR_TEXT);

    draw_row("status", s->installed ? "installed" : "not installed yet", 100.0f,
             s->installed ? CLR_LEAF : CLR_WARN);

    draw_text("goes to", 18.0f, 114.0f, 0.42f, CLR_MUTED);
    draw_text_right(s->dest ? s->dest : "", TOP_W - 18.0f, 114.0f, 0.38f, CLR_MUTED);

    // Below the panel: whatever is happening now.
    float y = 144.0f;

    if (s->busy_label)
    {
        draw_text(s->busy_label, 16.0f, y, 0.5f, CLR_SUN);
        y += 22.0f;
        draw_progress(16.0f, y, TOP_W - 32.0f, s->progress);
        y += 18.0f;
    }
    else if (ctx->update == UPDATE_AVAILABLE && s->latest_version)
    {
        snprintf(buf, sizeof(buf), "Version %s is available.", s->latest_version);
        draw_text(buf, 16.0f, y, 0.52f, CLR_SUN);
        y += 22.0f;
    }

    if (s->message && s->message[0])
    {
        draw_wrapped(s->message, 16.0f, y, TOP_W - 32.0f, 0.46f, 16.0f,
                     s->message_is_error ? CLR_ERROR : CLR_TEXT);
    }
}

static void draw_bottom_screen(const ui_screen *s, const ui_context *ctx, int selection)
{
    C2D_TargetClear(s_bottom, CLR_CANOPY);
    C2D_SceneBegin(s_bottom);

    C2D_DrawRectangle(0, 0, 0.0f, UI_SCREEN_W, UI_SCREEN_H,
                      CLR_FOREST, CLR_FOREST, CLR_CANOPY, CLR_CANOPY);

    ui_button buttons[UI_MAX_BUTTONS];
    int count = ui_buttons(ctx, buttons);

    if (count == 0)
    {
        // Busy. Nothing to press, so the screen's whole job is to prove the app
        // has not hung - which is exactly what the old one failed to do.
        draw_header(UI_SCREEN_W, "Please wait", NULL);

        const char *label = s->busy_label ? s->busy_label : ui_busy_label(ctx->update);
        draw_wrapped(label, 20.0f, 74.0f, UI_SCREEN_W - 40.0f, 0.52f, 18.0f, CLR_TEXT);

        draw_progress(20.0f, 128.0f, UI_SCREEN_W - 40.0f, s->progress);

        if (s->progress >= 0)
        {
            char pct[16];
            snprintf(pct, sizeof(pct), "%d%%", s->progress);
            draw_text_right(pct, UI_SCREEN_W - 20.0f, 142.0f, 0.46f, CLR_MUTED);
        }

        draw_wrapped("Do not turn the console off.",
                     20.0f, 172.0f, UI_SCREEN_W - 40.0f, 0.42f, 14.0f, CLR_MUTED);
        return;
    }

    draw_header(UI_SCREEN_W, "Verdant Pass", "choose an option");

    for (int i = 0; i < count; i++)
    {
        float x, y, w, h;
        ui_button_rect(i, &x, &y, &w, &h);

        bool chosen = (i == selection) && buttons[i].enabled;

        C2D_DrawRectSolid(x, y, 0.0f, w, h, chosen ? CLR_PANEL_HI : CLR_PANEL);

        // A bar down the left edge marks the highlight. It reads at a glance on
        // a small screen in a way a slightly lighter fill does not.
        if (chosen) C2D_DrawRectSolid(x, y, 0.1f, 4.0f, h, CLR_LEAF);

        draw_text(buttons[i].label, x + 14.0f, y + 8.0f, 0.56f,
                  buttons[i].enabled ? CLR_TEXT : CLR_MUTED);
        draw_text(buttons[i].hint, x + 14.0f, y + 29.0f, 0.38f, CLR_MUTED);
    }

    // The one thing that is not a button: what to do after installing.
    if (ctx->update != UPDATE_AVAILABLE && ctx->update != UPDATE_DONE)
    {
        draw_text("Then open Hotswap and pick Verdant Pass.",
                  UI_BTN_X, 222.0f, 0.38f, CLR_MUTED);
    }
}

// ---------------------------------------------------------------------------

bool ui_init(void)
{
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) return false;
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) { C3D_Fini(); return false; }
    C2D_Prepare();

    s_top    = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    s_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    s_text   = C2D_TextBufNew(4096);

    if (!s_top || !s_bottom || !s_text) { ui_exit(); return false; }
    return true;
}

void ui_exit(void)
{
    if (s_text) { C2D_TextBufDelete(s_text); s_text = NULL; }
    s_top = s_bottom = NULL;
    C2D_Fini();
    C3D_Fini();
}

void ui_frame(const ui_screen *screen, const ui_context *ctx, int selection)
{
    s_frame++;
    C2D_TextBufClear(s_text);

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    draw_top_screen(screen, ctx);
    draw_bottom_screen(screen, ctx, selection);
    C3D_FrameEnd(0);
}
