/* The launcher's always-on effects (camera_effects.h). Run with
   RECOMP_D3D_OCCLUSION=0.5 and RECOMP_D3D_BLOOM=0.3 (CMakeLists.txt). */
#include "camera_effects.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(bool ok, const char *what)
{
    if (!ok) {
        ++failures;
        fprintf(stderr, "camera effects always test failed: %s\n", what);
    }
}

int main(void)
{
    float occlusion = 0.0f, bloom = 0.0f;
    recomp_camera_effects_always(&occlusion, &bloom);
    check(fabsf(occlusion - 0.5f) < 1e-6f && fabsf(bloom - 0.3f) < 1e-6f, "reads the launcher's amounts");
    check(recomp_camera_effects_always_wanted(), "wanted");

    /* The camera not flying: its look stays off, its hour stays. */
    RecompCameraEffects idle;
    recomp_camera_effects_neutral(&idle);
    idle.filter = RECOMP_CAMERA_FILTER_SEPIA;
    idle.vignette = 0.8f;
    idle.exposure = 1.0f;
    idle.depth_of_field = true;
    idle.hour = 19.0f;
    recomp_camera_effects_publish(&idle);
    RecompCameraEffects taken;
    recomp_camera_effects_take(&taken);
    check(taken.active, "active in any scene");
    check(taken.filter == RECOMP_CAMERA_FILTER_NONE && taken.vignette == 0.0f && taken.exposure == 0.0f &&
        !taken.depth_of_field && taken.saturation == 1.0f, "the camera's look is not shown");
    check(taken.occlusion == 0.5f && taken.bloom == 0.3f, "the launcher's occlusion and bloom");
    check(taken.hour == 19.0f, "the hour others read stays");

    /* Flying: the stronger of the two, the camera's look kept. */
    RecompCameraEffects flying = idle;
    flying.active = true;
    flying.occlusion = 0.9f;
    flying.bloom = 0.1f;
    recomp_camera_effects_publish(&flying);
    recomp_camera_effects_take(&taken);
    check(taken.occlusion == 0.9f && taken.bloom == 0.3f, "the stronger of each");
    check(taken.filter == RECOMP_CAMERA_FILTER_SEPIA && taken.depth_of_field, "the camera's look while flying");

    if (failures == 0) printf("camera effects always test passed\n");
    return failures == 0 ? 0 : 1;
}
