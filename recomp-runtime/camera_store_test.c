#include "camera_store.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(bool ok, const char *what)
{
    if (!ok) {
        ++failures;
        fprintf(stderr, "camera store test failed: %s\n", what);
    }
}

int main(void)
{
    static RecompCameraStore kept;
    memset(&kept, 0, sizeof kept);
    recomp_camera_settings_default(&kept.settings);
    kept.settings.depth_of_field = true;
    kept.settings.exposure = -3;
    kept.settings.roll = 12.5f;
    kept.settings.filter = RECOMP_CAMERA_FILTER_SEPIA;
    kept.settings.motion_blur = 2u;
    kept.settings.mouth = 3u;
    kept.settings.move_her = true;
    kept.settings.record = true;
    RecompFreeCamera view = {{1.0f, 2.0f, 3.0f}, 0.25f, -0.1f, 0.7f, 2.0f, 4.0f};
    kept.spots[2] = view;
    kept.spot_saved[2] = true;
    RecompCameraEffects look = {0};
    look.exposure = 0.5f;
    look.hour = 18.0f;
    recomp_camera_path_add_look(&kept.path, &view, &look);
    view.position[0] = 9.0f;
    recomp_camera_path_add(&kept.path, &view);

    kept.poses[3].used = true;
    kept.poses[3].type = 14u;
    kept.poses[3].bones[2][13] = 1.25f;
    kept.poses[3].bones[2][15] = 1.0f;
    static char text[262144];
    const size_t length = recomp_camera_store_write(&kept, text, sizeof text);
    check(length > 0u && length < sizeof text, "writes the store");
    check(recomp_camera_store_write(&kept, text, 16u) == 0u, "says when it does not fit");
    recomp_camera_store_write(&kept, text, sizeof text);

    static RecompCameraStore read;
    memset(&read, 0, sizeof read);
    recomp_camera_settings_default(&read.settings);
    recomp_camera_store_read(text, &read);
    check(read.settings.depth_of_field && read.settings.exposure == -3 && read.settings.roll == 12.5f &&
        read.settings.filter == RECOMP_CAMERA_FILTER_SEPIA && read.settings.motion_blur == 2u &&
        read.settings.mouth == 3u, "settings come back");
    check(!read.settings.move_her && !read.settings.record, "moving her and recording are not kept");
    check(read.spot_saved[2] && !read.spot_saved[0] && read.spots[2].heading == 0.25f &&
        read.spots[2].position[2] == 3.0f, "spots come back");
    check(read.path.count == 2u && read.path.looked[0] && !read.path.looked[1] &&
        read.path.looks[0].hour == 18.0f && read.path.keys[1].position[0] == 9.0f, "the path and its looks come back");

    check(read.poses[3].used && read.poses[3].type == 14u && read.poses[3].bones[2][13] == 1.25f &&
        !read.poses[0].used, "the pose library comes back");

    /* Out of range, unknown and broken lines. */
    static RecompCameraStore odd;
    memset(&odd, 0, sizeof odd);
    recomp_camera_settings_default(&odd.settings);
    recomp_camera_store_read("slow 99\nsaturation 500\nnot_a_setting 3\nspot 7 1 2 3 4 5 6 7 8\nfocus_distance nan\n"
        "spot 1 1 2\nexposure -2\n", &odd);
    check(odd.settings.slow == 3u && odd.settings.saturation == 20u, "values are brought into range");
    check(odd.settings.exposure == -2, "reads past lines it does not know");
    check(!odd.spot_saved[1], "a short spot is skipped");
    check(odd.settings.focus_distance >= 0.1f && isfinite(odd.settings.focus_distance), "a bad focus distance is not kept");

    if (failures == 0) printf("camera store test passed\n");
    return failures == 0 ? 0 : 1;
}
