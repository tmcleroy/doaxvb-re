#include "stage_lights.h"
#include "time_of_day.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(int condition, const char *label)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", label);
        ++failures;
    }
}

static float at(const uint8_t *bytes, unsigned offset)
{
    float value;
    memcpy(&value, bytes + offset, 4);
    return value;
}

static uint32_t word(const uint8_t *bytes, unsigned offset)
{
    uint32_t value;
    memcpy(&value, bytes + offset, 4);
    return value;
}

int main(void)
{
    RecompStageLight light;
    memset(&light, 0, sizeof light);
    light.kind = 1u;
    light.ambient_alpha = 0.25f;
    light.ambient[0] = 0.1f; light.ambient[1] = 0.2f; light.ambient[2] = 0.3f;
    light.diffuse_alpha = 1.0f;
    light.diffuse[0] = 0.9f; light.diffuse[1] = 0.8f; light.diffuse[2] = 0.7f;
    light.specular_scale = 0.5f;
    light.range = 100.0f;
    light.attenuation[0] = 1.0f;
    uint8_t bytes[RECOMP_STAGE_LIGHT_SIZE], again[RECOMP_STAGE_LIGHT_SIZE];
    recomp_stage_light_write(&light, bytes);
    RecompStageLight read;
    recomp_stage_light_read(bytes, &read);
    recomp_stage_light_write(&read, again);
    check(memcmp(bytes, again, sizeof bytes) == 0, "a record reads and writes back the same");
    check(at(bytes, 0x18) == 0.9f && at(bytes, 0x54) == 0.5f, "fields sit at the game's offsets");

    const float toward[3] = {0.0f, 1.0f, 0.0f};
    uint8_t d3d[RECOMP_STAGE_D3D_LIGHT_SIZE];
    recomp_stage_light_to_d3d(&light, toward, d3d);
    check(word(d3d, 0x00) == 3u, "kind 1 is a D3D directional light");
    check(at(d3d, 0x04) == 0.9f && at(d3d, 0x10) == 1.0f, "diffuse and its alpha");
    check(at(d3d, 0x14) == 0.45f && at(d3d, 0x20) == 1.0f, "specular is diffuse times the scale");
    check(at(d3d, 0x24) == 0.1f && at(d3d, 0x30) == 0.25f, "ambient and its alpha");
    check(at(d3d, 0x44) == -1.0f, "the D3D direction points away from where the light comes from");
    check(at(d3d, 0x4C) == 100.0f && at(d3d, 0x54) == 1.0f, "range and attenuation");
    check(word(d3d, 0x6C) == 1u, "an on light");
    light.kind = 0u;
    recomp_stage_light_to_d3d(&light, toward, d3d);
    check(word(d3d, 0x00) == 0u && word(d3d, 0x6C) == 0u, "kind 0 is off");

    check(recomp_stage_ambient_color(1.0f, 0.0f, 2.0f) == 0xFFFF00FFu, "channels clamp at 0 and 1");
    check(recomp_stage_ambient_color(0.5f, 0.25f, -1.0f) == 0xFF7F3F00u, "channels truncate their 255ths");

    RecompTimeOfDayLook noon, sunset, between;
    recomp_time_of_day_look(recomp_time_of_day_game_hour(0u), &noon);
    recomp_time_of_day_look(recomp_time_of_day_game_hour(1u), &sunset);
    check(noon.tint[0] == 1.0f && noon.fill == 1.0f, "the game's day is the reference");
    recomp_time_of_day_look(0.5f * (recomp_time_of_day_game_hour(0u) + recomp_time_of_day_game_hour(1u)), &between);
    check(between.elevation < noon.elevation && between.elevation > sunset.elevation, "the sun sinks through the afternoon");
    RecompStageLight key = light;
    key.kind = 1u;
    key.diffuse[0] = 0.6f; key.diffuse[1] = 0.55f; key.diffuse[2] = 0.52f;
    recomp_time_of_day_relight(&noon, &noon, 0u, &key);
    check(fabsf(key.diffuse[2] - 0.52f) < 1e-5f, "the stage's own time changes nothing");
    recomp_time_of_day_relight(&noon, &sunset, 0u, &key);
    check(key.diffuse[2] < 0.52f * 0.8f && key.diffuse[0] > key.diffuse[2], "sunset warms the key light");
    float tint[3];
    recomp_time_of_day_tint(&noon, &sunset, tint);
    check(tint[0] > tint[2], "the baked scene warms at sunset");
    const float from[3] = {0.3f, 0.9f, -0.3f};
    float aimed[3];
    recomp_time_of_day_direction(from, 0.2f, aimed);
    check(fabsf(aimed[1] - sinf(0.2f)) < 1e-5f && fabsf(aimed[0] + aimed[2]) < 1e-5f, "elevation set, heading kept");

    if (failures) return 1;
    puts("stage lights: all passed");
    return 0;
}
