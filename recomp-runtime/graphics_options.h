#ifndef DOAXBV_RECOMP_GRAPHICS_OPTIONS_H
#define DOAXBV_RECOMP_GRAPHICS_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The graphics options a player can change while playing
   (docs/graphics-options.md): the launcher sets them at start
   (RECOMP_D3D_* variables), the in-game Graphics menu (F2, or Back + Start)
   changes them, and the presenters read them every frame. Changes are saved
   to the launcher's settings file (RECOMP_LAUNCHER_SETTINGS) so both agree.
   Amounts are 0 to 1; 0 is off. */
typedef struct RecompGraphicsOptions {
    bool anisotropy;
    float sun_soft;
    float occlusion;
    float bloom;
    float skin_soft;
    float skin_sheen;
    /* HDR output on an HDR display (Metal and Vulkan), its brightest
       highlight in multiples of SDR white, and SDR white in nits where the
       display needs one (HDR10, and extended linear outside macOS). */
    bool hdr;
    float hdr_peak;
    float hdr_white;
} RecompGraphicsOptions;

/* The current options (read from the environment the first time). */
void recomp_graphics_options_get(RecompGraphicsOptions *options);
/* Makes `options` current, brought into range. */
void recomp_graphics_options_set(const RecompGraphicsOptions *options);
/* Counts changes, for readers that cache. */
uint32_t recomp_graphics_options_changes(void);

/* The launcher's settings file with these options written into it: each key
   replaced where it is, or added before the closing brace. `text` is the
   file as it was (may be empty); false when it does not fit. */
bool recomp_graphics_options_json(const RecompGraphicsOptions *options, const char *text, char *out, size_t size);
/* Writes the options into RECOMP_LAUNCHER_SETTINGS; false when unset or it
   could not be written. */
bool recomp_graphics_options_save(const RecompGraphicsOptions *options);

#ifdef __cplusplus
}
#endif

#endif
