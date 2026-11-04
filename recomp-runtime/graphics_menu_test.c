/* The Graphics menu's rows (graphics_menu.h). */
#include "graphics_menu.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(bool ok, const char *what)
{
    if (!ok) {
        ++failures;
        fprintf(stderr, "graphics menu test failed: %s\n", what);
    }
}

int main(void)
{
    RecompGraphicsOptions o;
    memset(&o, 0, sizeof o);
    o.anisotropy = true;
    o.hdr_peak = 4.0f;
    o.hdr_white = 203.0f;
    o.skin_sheen = 1.0f;
    char text[128];

    recomp_graphics_menu_change(&o, RECOMP_GRAPHICS_SKIN_SHEEN, -3);
    check(o.skin_sheen > 0.69f && o.skin_sheen < 0.71f, "sheen down in tens");
    recomp_graphics_menu_describe(&o, RECOMP_GRAPHICS_SKIN_SHEEN, true, text, sizeof text);
    check(strcmp(text, "Skin sheen: 70%") == 0, "sheen as a percentage");
    recomp_graphics_menu_change(&o, RECOMP_GRAPHICS_SKIN_SHEEN, -20);
    recomp_graphics_menu_describe(&o, RECOMP_GRAPHICS_SKIN_SHEEN, true, text, sizeof text);
    check(o.skin_sheen == 0.0f && strcmp(text, "Skin sheen: Off") == 0, "down to off and no further");

    o.occlusion = 0.5f;
    recomp_graphics_menu_change(&o, RECOMP_GRAPHICS_OCCLUSION, 5);
    check(o.occlusion == 1.0f, "occlusion up to 100%");
    o.bloom = 0.33f; /* between steps, as a launcher may leave it */
    recomp_graphics_menu_change(&o, RECOMP_GRAPHICS_BLOOM, 1);
    check(o.bloom > 0.39f && o.bloom < 0.41f, "onto the steps");

    recomp_graphics_menu_describe(&o, RECOMP_GRAPHICS_SUN_SOFT, false, text, sizeof text);
    check(strcmp(text, "Soft sun shadows: Off") == 0, "soft shadows off");
    o.sun_soft = 0.5f;
    recomp_graphics_menu_describe(&o, RECOMP_GRAPHICS_SUN_SOFT, false, text, sizeof text);
    check(strstr(text, "needs Sun shadows") != NULL, "soft shadows say they need sun shadows");

    recomp_graphics_menu_change(&o, RECOMP_GRAPHICS_ANISOTROPY, 1);
    check(!o.anisotropy, "anisotropy toggles");
    recomp_graphics_menu_change(&o, RECOMP_GRAPHICS_HDR, -1);
    check(o.hdr, "HDR toggles");
    recomp_graphics_menu_change(&o, RECOMP_GRAPHICS_HDR_PEAK, -10);
    check(o.hdr_peak == 1.5f, "peak floor");
    recomp_graphics_menu_change(&o, RECOMP_GRAPHICS_HDR_WHITE, 2);
    check(o.hdr_white == 250.0f, "white in 25 nit steps from 203");
    recomp_graphics_menu_describe(&o, RECOMP_GRAPHICS_HDR_PEAK, true, text, sizeof text);
    check(strcmp(text, "HDR highlights: 1.5x SDR white") == 0, "peak text");

    if (failures == 0) printf("graphics menu test passed\n");
    return failures == 0 ? 0 : 1;
}
