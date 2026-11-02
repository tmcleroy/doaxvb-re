#include "time_of_day.h"

#include <math.h>
#include <string.h>

static const float PI = 3.14159265f;

/* Keyframes, in hours. Day, sunset and dusk are the game's own key lights,
   averaged over the beach, rock arch and jungle courts at each time
   (docs/time-of-day.md); morning, golden hour and night are made from them.
   The tint is the baked scene's colour relative to day. KEYFRAMES_FILLED */
static const struct {
    float hour;
    RecompTimeOfDayLook look;
} KEYS[RECOMP_TIME_OF_DAY_KEYS] = {
    { 6.5f, {{0.62f, 0.55f, 0.50f}, {0.33f, 0.31f, 0.34f}, 0.85f, 0.20f, {0.86f, 0.80f, 0.78f}}},
    {12.0f, {{0.60f, 0.55f, 0.52f}, {0.40f, 0.36f, 0.34f}, 1.00f, 1.05f, {1.00f, 1.00f, 1.00f}}},
    {16.5f, {{0.58f, 0.50f, 0.42f}, {0.35f, 0.29f, 0.28f}, 0.95f, 0.45f, {1.00f, 0.92f, 0.82f}}},
    {18.0f, {{0.50f, 0.45f, 0.35f}, {0.29f, 0.21f, 0.21f}, 0.90f, 0.15f, {0.95f, 0.78f, 0.62f}}},
    {19.0f, {{0.53f, 0.50f, 0.50f}, {0.35f, 0.35f, 0.38f}, 0.85f, 0.10f, {0.70f, 0.68f, 0.78f}}},
    {21.0f, {{0.22f, 0.26f, 0.38f}, {0.14f, 0.16f, 0.24f}, 0.50f, 0.90f, {0.30f, 0.34f, 0.50f}}},
};

static float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static void blend(const RecompTimeOfDayLook *a, const RecompTimeOfDayLook *b, float t, RecompTimeOfDayLook *out)
{
    for (unsigned c = 0u; c < 3u; ++c) {
        out->key[c] = lerp(a->key[c], b->key[c], t);
        out->ambient[c] = lerp(a->ambient[c], b->ambient[c], t);
        out->tint[c] = lerp(a->tint[c], b->tint[c], t);
    }
    out->fill = lerp(a->fill, b->fill, t);
    out->elevation = lerp(a->elevation, b->elevation, t);
}

void recomp_time_of_day_look(float hour, RecompTimeOfDayLook *look)
{
    if (!(hour > KEYS[0].hour)) {
        *look = KEYS[0].look;
        return;
    }
    for (unsigned i = 1u; i < RECOMP_TIME_OF_DAY_KEYS; ++i) {
        if (hour <= KEYS[i].hour) {
            const float t = (hour - KEYS[i - 1u].hour) / (KEYS[i].hour - KEYS[i - 1u].hour);
            blend(&KEYS[i - 1u].look, &KEYS[i].look, t, look);
            return;
        }
    }
    *look = KEYS[RECOMP_TIME_OF_DAY_KEYS - 1u].look;
}

float recomp_time_of_day_game_hour(unsigned time)
{
    /* Keyframes 1, 3 and 4 are the game's day, sunset and dusk. */
    static const unsigned keys[3] = {1u, 3u, 4u};
    return KEYS[keys[time < 3u ? time : 0u]].hour;
}

void recomp_time_of_day_relight(const RecompTimeOfDayLook *stage, const RecompTimeOfDayLook *target,
    unsigned slot, RecompStageLight *light)
{
    if (light->kind == 0u) return;
    if (slot == 0u) {
        /* Colours change by the target's over the stage time's, so each
           place and each of its rigs keeps its own character. */
        for (unsigned c = 0u; c < 3u; ++c) {
            const float key = stage->key[c] > 0.01f ? light->diffuse[c] / stage->key[c] : 1.0f;
            const float ambient = stage->ambient[c] > 0.01f ? light->ambient[c] / stage->ambient[c] : 1.0f;
            light->diffuse[c] = target->key[c] * key;
            light->ambient[c] = target->ambient[c] * ambient;
        }
        return;
    }
    const float fill = stage->fill > 0.01f ? target->fill / stage->fill : 1.0f;
    for (unsigned c = 0u; c < 3u; ++c) {
        /* Fills take the key light's change of colour too. */
        const float hue = stage->key[c] > 0.01f ? target->key[c] / stage->key[c] : 1.0f;
        light->diffuse[c] *= fill * hue;
    }
}

void recomp_time_of_day_direction(const float toward[3], float elevation, float out[3])
{
    float x = toward[0], z = toward[2];
    float level = sqrtf(x * x + z * z);
    if (!(level > 1e-5f)) {
        x = 0.0f;
        z = 1.0f;
        level = 1.0f;
    }
    const float up = sinf(elevation), across = cosf(elevation);
    /* The game's toward vector points up for a light above: keep its sign. */
    const float sign = toward[1] < 0.0f ? -1.0f : 1.0f;
    out[0] = x / level * across;
    out[1] = sign * up;
    out[2] = z / level * across;
    (void)PI;
}

void recomp_time_of_day_tint(const RecompTimeOfDayLook *stage, const RecompTimeOfDayLook *target, float tint[3])
{
    for (unsigned c = 0u; c < 3u; ++c)
        tint[c] = stage->tint[c] > 0.01f ? target->tint[c] / stage->tint[c] : 1.0f;
}
