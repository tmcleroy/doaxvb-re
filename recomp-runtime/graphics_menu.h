#ifndef DOAXBV_RECOMP_GRAPHICS_MENU_H
#define DOAXBV_RECOMP_GRAPHICS_MENU_H

#include "graphics_options.h"
#include "input_model.h"
#include "overlay_text.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The in-game Graphics menu (docs/graphics-options.md): F2, or Back and
   Start together, opens and closes it anywhere, free camera or not; B also
   closes it. Up and down choose a row, left and right change it (held to
   keep going). Changes show at once and are saved to the launcher's
   settings when it closes. While it is open the game reads a neutral pad. */
typedef enum RecompGraphicsMenuRow {
    RECOMP_GRAPHICS_OCCLUSION,
    RECOMP_GRAPHICS_BLOOM,
    RECOMP_GRAPHICS_SUN_SOFT,
    RECOMP_GRAPHICS_SKIN_SOFT,
    RECOMP_GRAPHICS_SKIN_SHEEN,
    RECOMP_GRAPHICS_ANISOTROPY,
    RECOMP_GRAPHICS_HDR,
    RECOMP_GRAPHICS_HDR_PEAK,
    RECOMP_GRAPHICS_HDR_WHITE,
    RECOMP_GRAPHICS_ROW_COUNT
} RecompGraphicsMenuRow;

/* Moves a row's value by `steps` (negative goes down), clamped. */
void recomp_graphics_menu_change(RecompGraphicsOptions *options, RecompGraphicsMenuRow row, int steps);
/* "Name: value" for a row. `sun_shadows`: whether the launcher's sun
   shadows are on (soft shadows soften them). */
void recomp_graphics_menu_describe(const RecompGraphicsOptions *options, RecompGraphicsMenuRow row,
    bool sun_shadows, char *text, size_t size);

/* Once an input poll on the first port (and once a frame while the scene is
   frozen): takes the menu's keys and buttons. True while the menu is open,
   when the caller gives the game a neutral pad. */
bool recomp_graphics_menu_input(const RecompInputGamepad *gamepad);
bool recomp_graphics_menu_open(void);
/* The menu as the overlay's panel, when open. */
void recomp_graphics_menu_fill(RecompOverlayText *text, bool controller);

#ifdef __cplusplus
}
#endif

#endif
