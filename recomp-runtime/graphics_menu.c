#include "graphics_menu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Each row's step and range; toggles step by one. */
static const struct {
    const char *name;
    float step, low, high;
} ROWS[RECOMP_GRAPHICS_ROW_COUNT] = {
    [RECOMP_GRAPHICS_OCCLUSION] = {"Ambient occlusion", 0.25f, 0.0f, 1.0f},
    [RECOMP_GRAPHICS_BLOOM] = {"Bloom and sun glare", 0.1f, 0.0f, 1.0f},
    [RECOMP_GRAPHICS_SUN_SOFT] = {"Soft sun shadows", 0.25f, 0.0f, 1.0f},
    [RECOMP_GRAPHICS_SKIN_SOFT] = {"Soft skin", 0.25f, 0.0f, 1.0f},
    [RECOMP_GRAPHICS_SKIN_SHEEN] = {"Skin sheen", 0.1f, 0.0f, 1.0f},
    [RECOMP_GRAPHICS_ANISOTROPY] = {"Anisotropic filtering", 1.0f, 0.0f, 1.0f},
    [RECOMP_GRAPHICS_HDR] = {"HDR output", 1.0f, 0.0f, 1.0f},
    [RECOMP_GRAPHICS_HDR_PEAK] = {"HDR highlights", 0.5f, 1.5f, 10.0f},
    [RECOMP_GRAPHICS_HDR_WHITE] = {"HDR SDR white", 25.0f, 100.0f, 400.0f},
};

static float *amount(RecompGraphicsOptions *o, RecompGraphicsMenuRow row)
{
    switch (row) {
    case RECOMP_GRAPHICS_OCCLUSION: return &o->occlusion;
    case RECOMP_GRAPHICS_BLOOM: return &o->bloom;
    case RECOMP_GRAPHICS_SUN_SOFT: return &o->sun_soft;
    case RECOMP_GRAPHICS_SKIN_SOFT: return &o->skin_soft;
    case RECOMP_GRAPHICS_SKIN_SHEEN: return &o->skin_sheen;
    case RECOMP_GRAPHICS_HDR_PEAK: return &o->hdr_peak;
    case RECOMP_GRAPHICS_HDR_WHITE: return &o->hdr_white;
    default: return NULL;
    }
}

void recomp_graphics_menu_change(RecompGraphicsOptions *options, RecompGraphicsMenuRow row, int steps)
{
    if (options == NULL || steps == 0 || row >= RECOMP_GRAPHICS_ROW_COUNT) return;
    if (row == RECOMP_GRAPHICS_ANISOTROPY) {
        if (steps % 2 != 0) options->anisotropy = !options->anisotropy;
        return;
    }
    if (row == RECOMP_GRAPHICS_HDR) {
        if (steps % 2 != 0) options->hdr = !options->hdr;
        return;
    }
    float *value = amount(options, row);
    /* Onto the row's steps first (a launcher amount may sit between them). */
    const float step = ROWS[row].step, low = ROWS[row].low;
    float at = low + roundf((*value - low) / step) * step + (float)steps * step;
    if (at < low) at = low;
    if (at > ROWS[row].high) at = ROWS[row].high;
    *value = roundf(at * 1000.0f) / 1000.0f;
}

void recomp_graphics_menu_describe(const RecompGraphicsOptions *o, RecompGraphicsMenuRow row,
    bool sun_shadows, char *text, size_t size)
{
    if (o == NULL || text == NULL || size == 0u || row >= RECOMP_GRAPHICS_ROW_COUNT) return;
    const char *name = ROWS[row].name;
    float value = 0.0f;
    switch (row) {
    case RECOMP_GRAPHICS_ANISOTROPY:
        snprintf(text, size, "%s: %s", name, o->anisotropy ? "On" : "Off");
        return;
    case RECOMP_GRAPHICS_HDR:
        snprintf(text, size, "%s: %s", name, o->hdr ? "On (an HDR display shows it)" : "Off");
        return;
    case RECOMP_GRAPHICS_HDR_PEAK:
        snprintf(text, size, "%s: %.1fx SDR white%s", name, (double)o->hdr_peak, o->hdr ? "" : " (HDR off)");
        return;
    case RECOMP_GRAPHICS_HDR_WHITE:
#ifdef __APPLE__
        snprintf(text, size, "%s: set by macOS", name);
#else
        snprintf(text, size, "%s: %.0f nits%s", name, (double)o->hdr_white, o->hdr ? "" : " (HDR off)");
#endif
        return;
    default:
        value = *amount((RecompGraphicsOptions *)o, row);
        break;
    }
    if (value <= 0.0f) snprintf(text, size, "%s: Off", name);
    else snprintf(text, size, "%s: %.0f%%%s", name, (double)(value * 100.0f),
        row == RECOMP_GRAPHICS_SUN_SOFT && !sun_shadows ? " (needs Sun shadows in the launcher)" : "");
}

#ifdef RECOMP_FULL_PROGRAM
#include "input_host_win32.h"

enum {
    PAD_UP = 0x0001u, PAD_DOWN = 0x0002u, PAD_LEFT = 0x0004u, PAD_RIGHT = 0x0008u,
    PAD_START = 0x0010u, PAD_BACK = 0x0020u,
    ANALOG_B_INDEX = 1u, ANALOG_DOWN = 0x20u,
    /* Polls (about a frame each) before a held direction repeats, and between repeats. */
    REPEAT_FIRST = 20, REPEAT_NEXT = 7,
};

static struct {
    bool open, toggle_held, close_held;
    int row;
    int held_rows, held_steps, held_polls;
} menu;

static bool sun_shadows_on(void)
{
    const char *value = getenv("RECOMP_D3D_SUN_SHADOW");
    return value != NULL && strcmp(value, "1") == 0;
}

/* One step on a press, then again while held. */
static int repeat(int direction, int *held, int *polls)
{
    if (direction == 0) {
        *held = 0;
        *polls = 0;
        return 0;
    }
    if (direction != *held) {
        *held = direction;
        *polls = 0;
        return direction;
    }
    ++*polls;
    if (*polls >= REPEAT_FIRST && (*polls - REPEAT_FIRST) % REPEAT_NEXT == 0) return direction;
    return 0;
}

bool recomp_graphics_menu_open(void)
{
    return menu.open;
}

static void close_menu(void)
{
    menu.open = false;
    RecompGraphicsOptions options;
    recomp_graphics_options_get(&options);
    if (recomp_graphics_options_save(&options))
        fprintf(stderr, "recomp graphics: saved to the launcher's settings\n");
    else
        fprintf(stderr, "recomp graphics: not saved (no launcher settings file); kept for this run\n");
}

bool recomp_graphics_menu_input(const RecompInputGamepad *gamepad)
{
    const uint16_t buttons = gamepad != NULL ? gamepad->buttons : 0u;
    const bool toggle = recomp_input_host_key(RECOMP_INPUT_HOST_KEY_F2) ||
        ((buttons & PAD_BACK) != 0u && (buttons & PAD_START) != 0u);
    if (toggle && !menu.toggle_held) {
        if (menu.open) close_menu();
        else {
            menu.open = true;
            menu.held_rows = menu.held_steps = 0;
            /* B held from before must be let go first. */
            menu.close_held = true;
        }
    }
    menu.toggle_held = toggle;
    if (!menu.open) return false;

    const bool close = gamepad != NULL && gamepad->analog_buttons[ANALOG_B_INDEX] >= ANALOG_DOWN;
    if (close && !menu.close_held) {
        menu.close_held = true;
        close_menu();
        return true;
    }
    menu.close_held = close;
    const bool up = recomp_input_host_key(RECOMP_INPUT_HOST_KEY_UP) || (buttons & PAD_UP) != 0u;
    const bool down = recomp_input_host_key(RECOMP_INPUT_HOST_KEY_DOWN) || (buttons & PAD_DOWN) != 0u;
    const bool left = recomp_input_host_key(RECOMP_INPUT_HOST_KEY_LEFT) || (buttons & PAD_LEFT) != 0u;
    const bool right = recomp_input_host_key(RECOMP_INPUT_HOST_KEY_RIGHT) || (buttons & PAD_RIGHT) != 0u;
    static int row_polls, step_polls;
    const int rows = repeat((int)down - (int)up, &menu.held_rows, &row_polls);
    if (rows != 0) menu.row = (menu.row + rows + RECOMP_GRAPHICS_ROW_COUNT) % RECOMP_GRAPHICS_ROW_COUNT;
    const int steps = repeat((int)right - (int)left, &menu.held_steps, &step_polls);
    /* On/off rows change once a press, not while held. */
    const bool toggle_row = menu.row == RECOMP_GRAPHICS_ANISOTROPY || menu.row == RECOMP_GRAPHICS_HDR;
    if (steps != 0 && (!toggle_row || step_polls == 0)) {
        RecompGraphicsOptions options;
        recomp_graphics_options_get(&options);
        recomp_graphics_menu_change(&options, (RecompGraphicsMenuRow)menu.row, steps);
        recomp_graphics_options_set(&options);
    }
    return true;
}

void recomp_graphics_menu_fill(RecompOverlayText *text, bool controller)
{
    if (text == NULL || !menu.open) return;
    recomp_overlay_set_title(text, "GRAPHICS");
    RecompGraphicsOptions options;
    recomp_graphics_options_get(&options);
    const bool sun = sun_shadows_on();
    for (int row = 0; row < RECOMP_GRAPHICS_ROW_COUNT; ++row) {
        char value[RECOMP_OVERLAY_LINE_SIZE];
        recomp_graphics_menu_describe(&options, (RecompGraphicsMenuRow)row, sun, value, sizeof value);
        recomp_overlay_add_line(text, "%s %s", row == menu.row ? ">" : " ", value);
    }
    recomp_overlay_set_hint(text, controller
        ? "Back+Start or B close and save  |  D-pad up/down choose  |  D-pad left/right change"
        : "F2 close and save  |  up/down choose  |  left/right change  |  resolution, MSAA, sun shadows: launcher");
}
#else
bool recomp_graphics_menu_input(const RecompInputGamepad *gamepad)
{
    (void)gamepad;
    return false;
}

bool recomp_graphics_menu_open(void)
{
    return false;
}

void recomp_graphics_menu_fill(RecompOverlayText *text, bool controller)
{
    (void)text;
    (void)controller;
}
#endif
