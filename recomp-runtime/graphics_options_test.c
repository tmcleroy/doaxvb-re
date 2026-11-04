/* The graphics options record and its launcher-file writer (graphics_options.h). */
#include "graphics_options.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(bool ok, const char *what)
{
    if (!ok) {
        ++failures;
        fprintf(stderr, "graphics options test failed: %s\n", what);
    }
}

static bool has(const char *text, const char *pair) { return strstr(text, pair) != NULL; }

int main(void)
{
    RecompGraphicsOptions o;
    recomp_graphics_options_get(&o);
    check(o.anisotropy && o.occlusion == 0.0f && !o.hdr && o.hdr_peak == 4.0f && o.hdr_white == 203.0f,
        "defaults without the environment");
    o.occlusion = 0.75f;
    o.skin_sheen = 0.3f;
    o.bloom = 2.0f; /* out of range */
    o.hdr = true;
    const uint32_t before = recomp_graphics_options_changes();
    recomp_graphics_options_set(&o);
    RecompGraphicsOptions got;
    recomp_graphics_options_get(&got);
    check(got.occlusion == 0.75f && got.bloom == 1.0f && got.hdr, "set, brought into range");
    check(recomp_graphics_options_changes() == before + 1u, "counts the change");

    /* The SDL launcher's file: keys replaced in place, new ones added. */
    const char *sdl = "{\n  \"height\": 1080,\n  \"msaa\": 4,\n  \"anisotropy\": true,\n"
        "  \"ambient_occlusion\": false,\n  \"skin_sheen\": true,\n  \"title_idle\": \"default\"\n}\n";
    char out[4096];
    check(recomp_graphics_options_json(&got, sdl, out, sizeof out), "writes the SDL launcher's file");
    check(has(out, "\"height\": 1080") && has(out, "\"title_idle\": \"default\""), "keeps the other keys");
    check(has(out, "\"ambient_occlusion\": true") && has(out, "\"ambient_occlusion_amount\": 0.75"),
        "occlusion on, with its amount");
    check(has(out, "\"skin_sheen\": true") && has(out, "\"skin_sheen_amount\": 0.30"), "sheen amount");
    check(has(out, "\"sun_soft\": false") && !has(out, "sun_soft_amount"), "an option off keeps no amount");
    check(has(out, "\"hdr\": true") && has(out, "\"hdr_peak\": 4.00") && has(out, "\"hdr_white\": 203"), "HDR");
    /* Still one object: one opening and one closing brace, no ",}" and no ",," */
    check(strchr(out, '{') == strrchr(out, '{') && strchr(out, '}') == strrchr(out, '}'), "one object");
    check(!has(out, ",\n}") && !has(out, ",,") && !has(out, "\n\n"), "tidy commas and lines");

    /* Writing again changes nothing (no keys added twice). */
    char again[4096];
    check(recomp_graphics_options_json(&got, out, again, sizeof again) && strcmp(out, again) == 0, "idempotent");

    /* An amount that was saved stays when the option is turned off. */
    RecompGraphicsOptions off = got;
    off.occlusion = 0.0f;
    char turned[4096];
    check(recomp_graphics_options_json(&off, out, turned, sizeof turned) &&
        has(turned, "\"ambient_occlusion\": false") && has(turned, "\"ambient_occlusion_amount\": 0.75"),
        "off keeps the amount for next time");

    /* The macOS launcher's sorted, pretty file, and no file at all. */
    const char *mac = "{\n  \"anisotropy\" : true,\n  \"bloom\" : false\n}";
    check(recomp_graphics_options_json(&got, mac, out, sizeof out) && has(out, "\"bloom\" : true") &&
        has(out, "\"anisotropy\" : true") && has(out, "\"bloom_amount\": 1.00"), "the macOS launcher's file");
    check(recomp_graphics_options_json(&got, "", out, sizeof out) && out[0] == '{' && has(out, "\"hdr\": true") &&
        !has(out, "{,"), "no file yet");
    check(!recomp_graphics_options_json(&got, sdl, out, 32u), "too small fails");

    if (failures == 0) printf("graphics options test passed\n");
    return failures == 0 ? 0 : 1;
}
