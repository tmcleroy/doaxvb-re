#include "camera_store.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* The settings kept, by name: their place in the struct and their kind.
   Moving a girl and recording are left out on purpose. */
enum { KIND_BOOL, KIND_WORD, KIND_INT, KIND_FLOAT };
#define FIELD(name, kind) {#name, offsetof(RecompCameraSettings, name), kind}
static const struct {
    const char *name;
    size_t offset;
    unsigned kind;
} FIELDS[] = {
    FIELD(depth_of_field, KIND_BOOL), FIELD(focus, KIND_WORD), FIELD(focus_distance, KIND_FLOAT),
    FIELD(aperture, KIND_WORD), FIELD(roll, KIND_FLOAT), FIELD(filter, KIND_WORD),
    FIELD(vignette, KIND_WORD), FIELD(grain, KIND_WORD), FIELD(letterbox, KIND_WORD),
    FIELD(orbit, KIND_WORD), FIELD(track, KIND_WORD), FIELD(hide_ball, KIND_BOOL),
    FIELD(hide_others, KIND_BOOL), FIELD(shot_size, KIND_WORD), FIELD(time_of_day, KIND_WORD),
    FIELD(light_angle, KIND_INT), FIELD(light_strength, KIND_WORD), FIELD(rim_light, KIND_WORD),
    FIELD(fill_light, KIND_WORD), FIELD(occlusion, KIND_WORD), FIELD(exposure, KIND_INT), FIELD(contrast, KIND_INT),
    FIELD(saturation, KIND_WORD), FIELD(bloom, KIND_WORD), FIELD(grid, KIND_WORD),
    FIELD(spot, KIND_WORD), FIELD(transparent, KIND_BOOL), FIELD(backdrop, KIND_WORD),
    FIELD(path_seconds, KIND_WORD), FIELD(slow, KIND_WORD), FIELD(motion_blur, KIND_WORD),
    FIELD(eye_contact, KIND_WORD), FIELD(face_each, KIND_BOOL), FIELD(eyes, KIND_WORD), FIELD(mouth, KIND_WORD),
};
#undef FIELD
enum { FIELD_COUNT = sizeof FIELDS / sizeof FIELDS[0] };
/* The enum settings are kept as words. */
_Static_assert(sizeof(RecompCameraFocus) == sizeof(unsigned), "focus is a word");
_Static_assert(sizeof(RecompCameraFilter) == sizeof(unsigned), "filter is a word");
_Static_assert(sizeof(RecompCameraOrbit) == sizeof(unsigned), "orbit is a word");

/* A view: place, heading, pitch, field of view, speed, focus. */
enum { VIEW_FLOATS = 8 };
static void view_floats(const RecompFreeCamera *view, float out[VIEW_FLOATS])
{
    const float values[VIEW_FLOATS] = {view->position[0], view->position[1], view->position[2], view->heading,
        view->pitch, view->fov, view->speed, view->focus};
    memcpy(out, values, sizeof values);
}

static bool view_from(const float in[VIEW_FLOATS], RecompFreeCamera *view)
{
    for (unsigned i = 0u; i < VIEW_FLOATS; ++i)
        if (!isfinite(in[i])) return false;
    if (!(in[5] > 0.0f) || !(in[6] > 0.0f)) return false;
    view->position[0] = in[0];
    view->position[1] = in[1];
    view->position[2] = in[2];
    view->heading = in[3];
    view->pitch = in[4];
    view->fov = in[5];
    view->speed = in[6];
    view->focus = in[7];
    return true;
}

/* A look: the effects' amounts and choices, in this order. */
enum { LOOK_FLOATS = 21 };
static void look_floats(const RecompCameraEffects *look, float out[LOOK_FLOATS])
{
    const float values[LOOK_FLOATS] = {
        look->active ? 1.0f : 0.0f, look->depth_of_field ? 1.0f : 0.0f, look->focus, look->aperture, look->fov,
        look->roll, (float)look->filter, look->vignette, look->grain, look->letterbox, look->hour,
        look->light_angle, look->light_strength, look->exposure, look->contrast, look->saturation, look->bloom,
        look->backdrop ? 1.0f : 0.0f, look->backdrop_rgb[0], look->backdrop_rgb[1], look->backdrop_rgb[2]};
    memcpy(out, values, sizeof values);
}

static bool look_from(const float in[LOOK_FLOATS], RecompCameraEffects *look)
{
    for (unsigned i = 0u; i < LOOK_FLOATS; ++i)
        if (!isfinite(in[i])) return false;
    memset(look, 0, sizeof *look);
    look->active = in[0] != 0.0f;
    look->depth_of_field = in[1] != 0.0f;
    look->focus = in[2];
    look->aperture = in[3];
    look->fov = in[4];
    look->roll = in[5];
    const int filter = (int)in[6];
    look->filter = filter >= 0 && filter < RECOMP_CAMERA_FILTER_COUNT ? (RecompCameraFilter)filter : RECOMP_CAMERA_FILTER_NONE;
    look->vignette = in[7];
    look->grain = in[8];
    look->letterbox = in[9];
    look->hour = in[10];
    look->light_angle = in[11];
    look->light_strength = in[12];
    look->exposure = in[13];
    look->contrast = in[14];
    look->saturation = in[15];
    look->bloom = in[16];
    look->backdrop = in[17] != 0.0f;
    look->backdrop_rgb[0] = in[18];
    look->backdrop_rgb[1] = in[19];
    look->backdrop_rgb[2] = in[20];
    return true;
}

typedef struct Writer {
    char *text;
    size_t size, length;
    bool full;
} Writer;

static void put(Writer *out, const char *format, ...)
{
    if (out->full) return;
    va_list args;
    va_start(args, format);
    const int wrote = vsnprintf(out->text + out->length, out->size - out->length, format, args);
    va_end(args);
    if (wrote < 0 || (size_t)wrote >= out->size - out->length) {
        out->full = true;
        return;
    }
    out->length += (size_t)wrote;
}

static void put_floats(Writer *out, const char *name, int index, const float *values, unsigned count)
{
    if (index >= 0) put(out, "%s %d", name, index);
    else put(out, "%s", name);
    /* %.9g keeps every float exactly. */
    for (unsigned i = 0u; i < count; ++i) put(out, " %.9g", (double)values[i]);
    put(out, "\n");
}

size_t recomp_camera_store_write(const RecompCameraStore *store, char *text, size_t size)
{
    if (store == NULL || text == NULL || size == 0u) return 0u;
    Writer out = {text, size, 0u, false};
    put(&out, "# DOAXBV free camera (docs/free-camera.md)\n");
    const uint8_t *settings = (const uint8_t *)&store->settings;
    for (unsigned i = 0u; i < FIELD_COUNT; ++i) {
        const void *at = settings + FIELDS[i].offset;
        switch (FIELDS[i].kind) {
        case KIND_BOOL: put(&out, "%s %d\n", FIELDS[i].name, *(const bool *)at ? 1 : 0); break;
        case KIND_WORD: put(&out, "%s %u\n", FIELDS[i].name, *(const unsigned *)at); break;
        case KIND_INT: put(&out, "%s %d\n", FIELDS[i].name, *(const int *)at); break;
        default: put(&out, "%s %.9g\n", FIELDS[i].name, (double)*(const float *)at); break;
        }
    }
    for (unsigned i = 0u; i < RECOMP_CAMERA_SPOTS; ++i) {
        if (!store->spot_saved[i]) continue;
        float values[VIEW_FLOATS];
        view_floats(&store->spots[i], values);
        put_floats(&out, "spot", (int)i, values, VIEW_FLOATS);
    }
    for (unsigned i = 0u; i < store->path.count && i < RECOMP_CAMERA_PATH_KEYS; ++i) {
        float values[VIEW_FLOATS];
        view_floats(&store->path.keys[i], values);
        put_floats(&out, "path_view", -1, values, VIEW_FLOATS);
        if (store->path.looked[i]) {
            float look[LOOK_FLOATS];
            look_floats(&store->path.looks[i], look);
            put_floats(&out, "path_look", -1, look, LOOK_FLOATS);
        }
    }
    return out.full ? 0u : out.length;
}

/* Reads up to `count` floats after the name; how many it read. */
static unsigned read_floats(const char *from, float *values, unsigned count)
{
    unsigned read = 0u;
    char *end = NULL;
    while (read < count) {
        const float value = strtof(from, &end);
        if (end == from) break;
        values[read++] = value;
        from = end;
    }
    return read;
}

void recomp_camera_store_read(const char *text, RecompCameraStore *store)
{
    if (text == NULL || store == NULL) return;
    bool path_started = false;
    uint8_t *settings = (uint8_t *)&store->settings;
    for (const char *line = text; *line != '\0';) {
        const char *end = strchr(line, '\n');
        const size_t length = end != NULL ? (size_t)(end - line) : strlen(line);
        char buffer[512];
        if (length < sizeof buffer) {
            memcpy(buffer, line, length);
            buffer[length] = '\0';
            char name[32];
            int used = 0;
            if (buffer[0] != '#' && sscanf(buffer, "%31s%n", name, &used) == 1) {
                const char *rest = buffer + used;
                if (strcmp(name, "spot") == 0) {
                    char *after = NULL;
                    const long index = strtol(rest, &after, 10);
                    float values[VIEW_FLOATS];
                    if (after != rest && index >= 0 && index < RECOMP_CAMERA_SPOTS &&
                        read_floats(after, values, VIEW_FLOATS) == VIEW_FLOATS &&
                        view_from(values, &store->spots[index]))
                        store->spot_saved[index] = true;
                } else if (strcmp(name, "path_view") == 0) {
                    /* The file's path replaces the one held. */
                    if (!path_started) {
                        store->path.count = 0u;
                        path_started = true;
                    }
                    float values[VIEW_FLOATS];
                    RecompFreeCamera view;
                    if (store->path.count < RECOMP_CAMERA_PATH_KEYS &&
                        read_floats(rest, values, VIEW_FLOATS) == VIEW_FLOATS && view_from(values, &view))
                        recomp_camera_path_add(&store->path, &view);
                } else if (strcmp(name, "path_look") == 0) {
                    float values[LOOK_FLOATS];
                    const unsigned last = store->path.count - 1u;
                    if (path_started && store->path.count != 0u &&
                        read_floats(rest, values, LOOK_FLOATS) == LOOK_FLOATS &&
                        look_from(values, &store->path.looks[last]))
                        store->path.looked[last] = true;
                } else {
                    for (unsigned i = 0u; i < FIELD_COUNT; ++i) {
                        if (strcmp(name, FIELDS[i].name) != 0) continue;
                        void *at = settings + FIELDS[i].offset;
                        char *after = NULL;
                        if (FIELDS[i].kind == KIND_FLOAT) {
                            const float value = strtof(rest, &after);
                            if (after != rest && isfinite(value)) *(float *)at = value;
                        } else {
                            const long value = strtol(rest, &after, 10);
                            if (after == rest) break;
                            if (FIELDS[i].kind == KIND_BOOL) *(bool *)at = value != 0;
                            else if (FIELDS[i].kind == KIND_INT) *(int *)at = (int)value;
                            else if (value >= 0) *(unsigned *)at = (unsigned)value;
                        }
                        break;
                    }
                }
            }
        }
        line += length;
        if (*line == '\n') ++line;
    }
    /* Into range: a row moved by nothing is clamped or wrapped as the list
       would leave it. The focus distance row would make the focus manual,
       so it is bounded here. */
    for (int row = 0; row < RECOMP_CAMERA_SETTING_COUNT; ++row)
        if (row != RECOMP_CAMERA_SETTING_FOCUS_DISTANCE)
            recomp_camera_settings_change(&store->settings, (RecompCameraSettingRow)row, 0, NULL, 0.0f);
    float *focus = &store->settings.focus_distance;
    if (!(*focus >= 0.1f)) *focus = 0.1f;
    if (*focus > 1000.0f) *focus = 1000.0f;
    store->settings.move_her = false;
    store->settings.record = false;
}

static FILE *open_store(const char *root, const char *suffix, const char *mode)
{
    char path[1100];
    if (root == NULL ||
        snprintf(path, sizeof path, "%s/.recomp-storage/free-camera-v1.txt%s", root, suffix) >= (int)sizeof path)
        return NULL;
#ifdef _WIN32
    wchar_t wide[1100], wide_mode[8];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, 1100) == 0 ||
        MultiByteToWideChar(CP_UTF8, 0, mode, -1, wide_mode, 8) == 0) return NULL;
    return _wfopen(wide, wide_mode);
#else
    return fopen(path, mode);
#endif
}

bool recomp_camera_store_load(const char *root, RecompCameraStore *store)
{
    FILE *file = open_store(root, "", "rb");
    if (file == NULL) return false;
    static char text[64 * 1024];
    const size_t length = fread(text, 1u, sizeof text - 1u, file);
    fclose(file);
    text[length] = '\0';
    recomp_camera_store_read(text, store);
    return true;
}

bool recomp_camera_store_save(const char *root, const RecompCameraStore *store)
{
    static char text[64 * 1024];
    const size_t length = recomp_camera_store_write(store, text, sizeof text);
    if (length == 0u) return false;
    FILE *file = open_store(root, ".new", "wb");
    if (file == NULL) return false;
    const bool wrote = fwrite(text, 1u, length, file) == length;
    const bool closed = fclose(file) == 0;
    char from[1100], to[1100];
    snprintf(from, sizeof from, "%s/.recomp-storage/free-camera-v1.txt.new", root);
    snprintf(to, sizeof to, "%s/.recomp-storage/free-camera-v1.txt", root);
    if (!wrote || !closed) {
        remove(from);
        return false;
    }
#ifdef _WIN32
    wchar_t wide_from[1100], wide_to[1100];
    if (MultiByteToWideChar(CP_UTF8, 0, from, -1, wide_from, 1100) == 0 ||
        MultiByteToWideChar(CP_UTF8, 0, to, -1, wide_to, 1100) == 0) return false;
    return MoveFileExW(wide_from, wide_to, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(from, to) == 0;
#endif
}
