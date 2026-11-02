#include "camera_effects.h"
#include "camera_settings.h"

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

int main(void)
{
    RecompCameraEffects effects;
    memset(&effects, 0, sizeof effects);
    effects.aperture = 2.0f;
    /* 27 degrees vertical on a 24 mm sensor is a 50 mm lens. */
    effects.fov = 2.0f * atanf(12.0f / 50.0f);
    const float scale = recomp_camera_effects_blur_scale(&effects, 2.5f, 1080.0f);
    /* f^2 / (N (s - f)) / sensor * height = 0.0025 / (2 * 2.45) / 0.024 * 1080. */
    check(fabsf(scale - 22.96f) < 0.05f, "a 50 mm f/2 lens focused at 2.5 m");
    effects.aperture = 4.0f;
    check(fabsf(recomp_camera_effects_blur_scale(&effects, 2.5f, 1080.0f) - scale * 0.5f) < 0.05f,
        "two stops down halves the blur");
    check(recomp_camera_effects_blur_scale(&effects, 0.0f, 1080.0f) == 0.0f, "no focus, no blur");
    effects.aperture = 0.0f;
    check(recomp_camera_effects_blur_scale(&effects, 2.5f, 1080.0f) == 0.0f, "no aperture, no blur");

    float matrix[3][4];
    recomp_camera_effects_filter_matrix(RECOMP_CAMERA_FILTER_NONE, matrix);
    check(matrix[0][0] == 1.0f && matrix[1][1] == 1.0f && matrix[2][2] == 1.0f && matrix[0][1] == 0.0f &&
        matrix[2][3] == 0.0f, "no filter is the identity");
    recomp_camera_effects_filter_matrix(RECOMP_CAMERA_FILTER_MONO, matrix);
    check(fabsf(matrix[0][1] - matrix[2][1]) < 1e-6f, "black and white gives every channel the same mix");
    check(strcmp(recomp_camera_effects_filter_name(RECOMP_CAMERA_FILTER_SEPIA), "Sepia") == 0, "names");
    check(strcmp(recomp_camera_effects_filter_name((RecompCameraFilter)99), "None") == 0, "out of range");

    effects.active = true;
    effects.vignette = 0.5f;
    recomp_camera_effects_publish(&effects);
    RecompCameraEffects taken;
    recomp_camera_effects_take(&taken);
    check(taken.active && taken.vignette == 0.5f, "a published copy is taken whole");

    RecompCameraSettings settings;
    RecompCameraEffects drawn;
    float fov = 1.0f;
    recomp_camera_settings_default(&settings);
    recomp_camera_settings_effects(&settings, fov, 0.0f, &drawn);
    check(!drawn.active, "the defaults change nothing");
    recomp_camera_settings_change(&settings, RECOMP_CAMERA_SETTING_DEPTH_OF_FIELD, 1, &fov, 0.0f);
    recomp_camera_settings_effects(&settings, fov, 0.0f, &drawn);
    check(drawn.active && drawn.depth_of_field && drawn.focus == RECOMP_CAMERA_FOCUS_SUBJECT && drawn.aperture == 2.0f,
        "depth of field starts on her at f/2");
    recomp_camera_settings_change(&settings, RECOMP_CAMERA_SETTING_FOCUS_DISTANCE, 1, &fov, 4.0f);
    check(settings.focus == RECOMP_CAMERA_FOCUS_MANUAL && fabsf(settings.focus_distance - 4.2f) < 1e-4f,
        "changing the distance goes manual from where she was");
    recomp_camera_settings_change(&settings, RECOMP_CAMERA_SETTING_APERTURE, -5, &fov, 0.0f);
    recomp_camera_settings_effects(&settings, fov, 0.0f, &drawn);
    check(drawn.aperture == 1.4f && drawn.focus > 4.19f, "the aperture stops at f/1.4");
    recomp_camera_settings_change(&settings, RECOMP_CAMERA_SETTING_FIELD_OF_VIEW, 10, &fov, 0.0f);
    check(fabsf(fov * 180.0f / 3.14159265f - 47.0f) < 0.01f, "field of view steps in whole degrees");
    recomp_camera_settings_change(&settings, RECOMP_CAMERA_SETTING_ROLL, 100, &fov, 0.0f);
    check(settings.roll == 45.0f, "roll stops at 45 degrees");
    recomp_camera_settings_change(&settings, RECOMP_CAMERA_SETTING_FILTER, -1, &fov, 0.0f);
    check(settings.filter == RECOMP_CAMERA_FILTER_SEPIA, "filters wrap around");
    recomp_camera_settings_change(&settings, RECOMP_CAMERA_SETTING_LETTERBOX, 1, &fov, 0.0f);
    recomp_camera_settings_effects(&settings, fov, 0.0f, &drawn);
    check(fabsf(drawn.letterbox - 2.39f) < 1e-4f, "letterbox 2.39:1");
    recomp_camera_settings_change(&settings, RECOMP_CAMERA_SETTING_ORBIT, -1, &fov, 0.0f);
    check(settings.orbit == RECOMP_CAMERA_ORBIT_TURNTABLE, "orbit wraps to turntable");
    recomp_camera_settings_change(&settings, RECOMP_CAMERA_SETTING_HIDE_OTHERS, 1, &fov, 0.0f);
    check(settings.hide_others && !settings.hide_ball, "hiding toggles one thing");
    recomp_camera_settings_change(&settings, RECOMP_CAMERA_SETTING_SHOT_SIZE, 9, &fov, 0.0f);
    check(settings.shot_size == 4u, "screenshots stop at 4x");
    recomp_camera_settings_change(&settings, RECOMP_CAMERA_SETTING_TIME_OF_DAY, 1, &fov, 0.0f);
    recomp_camera_settings_effects(&settings, fov, 0.0f, &drawn);
    check(drawn.hour == 6.0f, "the first time of day is 06:00");
    recomp_camera_settings_change(&settings, RECOMP_CAMERA_SETTING_TIME_OF_DAY, 100, &fov, 0.0f);
    recomp_camera_settings_effects(&settings, fov, 0.0f, &drawn);
    check(drawn.hour == 22.0f, "the last is 22:00");
    char line[96];
    recomp_camera_settings_describe(&settings, RECOMP_CAMERA_SETTING_APERTURE, fov, 0.0f, line, sizeof line);
    check(strcmp(line, "Aperture: f/1.4") == 0, "rows describe themselves");

    if (failures) return 1;
    puts("camera effects: all passed");
    return 0;
}
