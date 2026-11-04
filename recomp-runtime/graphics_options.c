#include "graphics_options.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static volatile LONG options_lock;
static void lock(void) { while (InterlockedCompareExchange(&options_lock, 1, 0) != 0) {} }
static void unlock(void) { InterlockedExchange(&options_lock, 0); }
#else
#include <stdatomic.h>
static atomic_flag options_lock = ATOMIC_FLAG_INIT;
static void lock(void) { while (atomic_flag_test_and_set_explicit(&options_lock, memory_order_acquire)) {} }
static void unlock(void) { atomic_flag_clear_explicit(&options_lock, memory_order_release); }
#endif

static RecompGraphicsOptions current;
static bool read_once;
static uint32_t changes;

static float clampf(float value, float low, float high)
{
    if (!(value >= low)) return low;  /* NaN too */
    return value > high ? high : value;
}

static float env_float(const char *name, float fallback)
{
    const char *text = getenv(name);
    if (text == NULL || text[0] == '\0') return fallback;
    char *end = NULL;
    const float value = strtof(text, &end);
    return end != text && isfinite(value) ? value : fallback;
}

static void bring_into_range(RecompGraphicsOptions *o)
{
    o->sun_soft = clampf(o->sun_soft, 0.0f, 1.0f);
    o->occlusion = clampf(o->occlusion, 0.0f, 1.0f);
    o->bloom = clampf(o->bloom, 0.0f, 1.0f);
    o->skin_soft = clampf(o->skin_soft, 0.0f, 1.0f);
    o->skin_sheen = clampf(o->skin_sheen, 0.0f, 1.0f);
    o->hdr_peak = clampf(o->hdr_peak, 1.0f, 16.0f);
    o->hdr_white = clampf(o->hdr_white, 80.0f, 600.0f);
}

/* Called with the lock held. */
static void read_environment(void)
{
    if (read_once) return;
    read_once = true;
    const char *anisotropy = getenv("RECOMP_D3D_ANISOTROPY");
    current.anisotropy = anisotropy == NULL || strcmp(anisotropy, "0") != 0;
    current.sun_soft = env_float("RECOMP_D3D_SUN_SOFT", 0.0f);
    current.occlusion = env_float("RECOMP_D3D_OCCLUSION", 0.0f);
    current.bloom = env_float("RECOMP_D3D_BLOOM", 0.0f);
    current.skin_soft = env_float("RECOMP_D3D_SKIN_SOFT", 0.0f);
    current.skin_sheen = env_float("RECOMP_D3D_SKIN_SHEEN", 0.0f);
    const char *hdr = getenv("RECOMP_D3D_HDR");
    current.hdr = hdr != NULL && hdr[0] != '\0' && strcmp(hdr, "0") != 0;
    current.hdr_peak = env_float("RECOMP_D3D_HDR_PEAK", 4.0f);
    current.hdr_white = env_float("RECOMP_D3D_HDR_WHITE", 203.0f);
    bring_into_range(&current);
}

void recomp_graphics_options_get(RecompGraphicsOptions *options)
{
    if (options == NULL) return;
    lock();
    read_environment();
    *options = current;
    unlock();
}

void recomp_graphics_options_set(const RecompGraphicsOptions *options)
{
    if (options == NULL) return;
    RecompGraphicsOptions next = *options;
    bring_into_range(&next);
    lock();
    read_once = true;
    current = next;
    ++changes;
    unlock();
}

uint32_t recomp_graphics_options_changes(void)
{
    lock();
    const uint32_t count = changes;
    unlock();
    return count;
}

/* Replaces the value of "key" in `text` (in place, within `size`), or adds
   the pair before the object's closing brace. */
static bool put_value(char *text, size_t size, const char *key, const char *value)
{
    char quoted[64];
    snprintf(quoted, sizeof quoted, "\"%s\"", key);
    size_t length = strlen(text);
    char *at = strstr(text, quoted);
    if (at != NULL) {
        char *colon = strchr(at + strlen(quoted), ':');
        if (colon == NULL) return false;
        char *start = colon + 1;
        while (*start == ' ' || *start == '\t') ++start;
        char *end = start;
        while (*end != '\0' && *end != ',' && *end != '}' && *end != '\r' && *end != '\n') ++end;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;
        const size_t old_length = (size_t)(end - start), new_length = strlen(value);
        if (length - old_length + new_length + 1u > size) return false;
        memmove(start + new_length, end, length - (size_t)(end - text) + 1u);
        memcpy(start, value, new_length);
        return true;
    }
    char *brace = strrchr(text, '}');
    if (brace == NULL) return false;
    /* A comma after the last pair, unless the object is empty. */
    char *before = brace;
    while (before > text && (before[-1] == ' ' || before[-1] == '\t' || before[-1] == '\r' || before[-1] == '\n')) --before;
    const bool empty = before > text && before[-1] == '{';
    char pair[160];
    snprintf(pair, sizeof pair, "%s\n  %s: %s\n", empty ? "" : ",", quoted, value);
    const size_t pair_length = strlen(pair), tail = length - (size_t)(before - text) + 1u;
    if (length + pair_length + 1u > size) return false;
    memmove(before + pair_length, before, tail);
    memcpy(before, pair, pair_length);
    /* Drop the whitespace that stood before the brace: the pair ends the line. */
    char *closing = strrchr(text, '}');
    char *gap = closing;
    while (gap > text && (gap[-1] == ' ' || gap[-1] == '\t' || gap[-1] == '\r' || gap[-1] == '\n')) --gap;
    if (gap < closing && gap[-1] != '\n') {
        memmove(gap + 1, closing, strlen(closing) + 1u);
        gap[0] = '\n';
    } else if (gap < closing) {
        memmove(gap, closing, strlen(closing) + 1u);
    }
    return true;
}

bool recomp_graphics_options_json(const RecompGraphicsOptions *options, const char *text, char *out, size_t size)
{
    if (options == NULL || out == NULL || size < 4u) return false;
    const char *start = text != NULL ? text : "";
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') ++start;
    if (*start == '\0') start = "{\n}\n";
    if (strlen(start) + 1u > size) return false;
    strcpy(out, start);
    RecompGraphicsOptions o = *options;
    bring_into_range(&o);
    char number[32];
    bool ok = put_value(out, size, "anisotropy", o.anisotropy ? "true" : "false");
    const struct { const char *on, *amount; float value; } amounts[] = {
        {"sun_soft", "sun_soft_amount", o.sun_soft},
        {"ambient_occlusion", "ambient_occlusion_amount", o.occlusion},
        {"bloom", "bloom_amount", o.bloom},
        {"skin_soft", "skin_soft_amount", o.skin_soft},
        {"skin_sheen", "skin_sheen_amount", o.skin_sheen},
    };
    for (size_t i = 0; i < sizeof amounts / sizeof amounts[0]; ++i) {
        ok = ok && put_value(out, size, amounts[i].on, amounts[i].value > 0.0f ? "true" : "false");
        /* Off keeps the amount it had, so turning it on again restores it. */
        if (amounts[i].value > 0.0f) {
            snprintf(number, sizeof number, "%.2f", (double)amounts[i].value);
            ok = ok && put_value(out, size, amounts[i].amount, number);
        }
    }
    ok = ok && put_value(out, size, "hdr", o.hdr ? "true" : "false");
    snprintf(number, sizeof number, "%.2f", (double)o.hdr_peak);
    ok = ok && put_value(out, size, "hdr_peak", number);
    snprintf(number, sizeof number, "%.0f", (double)o.hdr_white);
    ok = ok && put_value(out, size, "hdr_white", number);
    return ok;
}

bool recomp_graphics_options_save(const RecompGraphicsOptions *options)
{
    const char *path = getenv("RECOMP_LAUNCHER_SETTINGS");
    if (path == NULL || path[0] == '\0' || options == NULL) return false;
    static char before[16384], after[16384];
    size_t length = 0u;
    FILE *file = fopen(path, "rb");
    if (file != NULL) {
        length = fread(before, 1u, sizeof before - 1u, file);
        fclose(file);
    }
    before[length] = '\0';
    if (!recomp_graphics_options_json(options, before, after, sizeof after)) return false;
    char temporary[1024];
    snprintf(temporary, sizeof temporary, "%s.tmp", path);
    file = fopen(temporary, "wb");
    if (file == NULL) return false;
    const size_t written = fwrite(after, 1u, strlen(after), file);
    const bool closed = fclose(file) == 0;
    if (written != strlen(after) || !closed) {
        remove(temporary);
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING)) {
        remove(temporary);
        return false;
    }
#else
    if (rename(temporary, path) != 0) {
        remove(temporary);
        return false;
    }
#endif
    return true;
}
