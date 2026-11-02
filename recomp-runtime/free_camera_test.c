#include "free_camera.h"
#include "camera_path.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(bool condition, const char *what)
{
    if (!condition) {
        fprintf(stderr, "free camera test failed: %s\n", what);
        ++failures;
    }
}

static bool near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

int main(void)
{
    RecompFreeCamera camera;
    RecompFreeCameraControls controls;
    float target[3];

    const float eye[3] = {1.0f, 2.0f, 3.0f};
    const float ahead[3] = {1.0f, 2.0f, 13.0f};
    check(recomp_free_camera_begin(&camera, eye, ahead, 0.8f), "begin from a level view");
    check(near(camera.heading, 0.0f) && near(camera.pitch, 0.0f), "level view down +Z");
    check(near(camera.focus, 10.0f) && near(camera.speed, 5.0f), "focus and speed from the gap");
    recomp_free_camera_target(&camera, target);
    check(near(target[0], 1.0f) && near(target[1], 2.0f) && near(target[2], 13.0f),
        "target reproduces the game's target");

    memset(&controls, 0, sizeof controls);
    controls.move[2] = 1.0f;
    recomp_free_camera_step(&camera, &controls, 1.0f);
    check(near(camera.position[2], 8.0f), "forward moves speed units a second");

    memset(&controls, 0, sizeof controls);
    controls.move[0] = 1.0f;
    controls.fast = true;
    recomp_free_camera_step(&camera, &controls, 0.5f);
    check(near(camera.position[0], -9.0f), "right strafes along -X at a zero heading, fast x4");

    memset(&controls, 0, sizeof controls);
    controls.look[0] = 1.0f;
    recomp_free_camera_step(&camera, &controls, 0.1f);
    check(camera.heading < 0.0f, "looking right turns the heading negative");
    recomp_free_camera_target(&camera, target);
    check(target[0] < camera.position[0], "looking right turns toward -X, the right-hand side");

    memset(&controls, 0, sizeof controls);
    float heading = camera.heading;
    controls.turn[0] = 0.25f;
    recomp_free_camera_step(&camera, &controls, 0.1f);
    check(near(camera.heading, heading - 0.25f), "a turn applies its radians at once");
    controls.turn[0] = NAN;
    recomp_free_camera_step(&camera, &controls, 0.1f);
    check(near(camera.heading, heading - 0.25f), "a non-finite turn is ignored");

    memset(&controls, 0, sizeof controls);
    controls.look[1] = 1.0f;
    for (int i = 0; i < 200; ++i) recomp_free_camera_step(&camera, &controls, 0.1f);
    check(camera.pitch < 1.5708f && camera.pitch > 1.5f, "pitch stops short of straight up");

    memset(&controls, 0, sizeof controls);
    controls.zoom = 1.0f;
    float before = camera.fov;
    recomp_free_camera_step(&camera, &controls, 0.5f);
    check(camera.fov < before, "zooming in narrows the field of view");

    const float origin[3] = {0.0f, 0.0f, 0.0f};
    const float to_left[3] = {5.0f, 0.0f, 0.0f};
    check(recomp_free_camera_begin(&camera, origin, ahead, 0.8f), "begin at the origin");
    check(recomp_free_camera_look_at(&camera, to_left), "look at a point");
    recomp_free_camera_target(&camera, target);
    check(near(camera.position[0], 0.0f) && target[0] > 9.9f && fabsf(target[2]) < 1e-3f,
        "looking at a point keeps the position and faces it");
    check(!recomp_free_camera_look_at(&camera, origin), "looking at the camera's own position is refused");

    const float far_point[3] = {0.0f, 0.0f, 20.0f};
    check(recomp_free_camera_begin(&camera, origin, ahead, 0.2f), "begin for framing");
    check(recomp_free_camera_frame(&camera, far_point, 2.5f, 0.8f), "frame a far point");
    check(near(camera.position[2], 17.5f) && near(camera.fov, 0.8f) && near(camera.focus, 2.5f),
        "framing moves to the distance along the line of sight");

    RecompFreeCamera untouched = camera;
    check(!recomp_free_camera_begin(&camera, eye, eye, 0.8f), "eye on target is refused");
    check(memcmp(&untouched, &camera, sizeof camera) == 0, "a refused begin leaves the camera");
    const float nan_target[3] = {NAN, 0.0f, 0.0f};
    check(!recomp_free_camera_begin(&camera, eye, nan_target, 0.8f), "a non-finite target is refused");

    const float above[3] = {1.0f, 12.0f, 3.0f};
    check(recomp_free_camera_begin(&camera, eye, above, 0.8f), "begin looking straight up");
    check(camera.pitch < 1.5708f, "a vertical view is clamped below straight up");

    /* Orbit: start 4 units in front of a camera at the origin looking down +Z. */
    const float down_z[3] = {0.0f, 0.0f, 10.0f};
    RecompFreeCameraOrbit orbit;
    RecompFreeCameraControls still = {0};
    check(recomp_free_camera_begin(&camera, origin, down_z, 0.8f), "begin for orbiting");
    check(recomp_free_camera_orbit_begin(&camera, 4.0f, &orbit), "begin an orbit");
    check(near(orbit.pivot[2], 4.0f) && near(orbit.distance, 4.0f), "the pivot is the distance ahead");
    recomp_free_camera_orbit_step(&camera, &orbit, &still, 0.0f, 1.0f / 60.0f);
    check(near(camera.position[0], 0.0f) && near(camera.position[2], 0.0f), "a still orbit keeps the camera");
    /* A quarter turn of turntable puts the camera beside the pivot, facing it. */
    recomp_free_camera_orbit_step(&camera, &orbit, &still, 1.5707963f, 1.0f);
    recomp_free_camera_target(&camera, target);
    check(near(fabsf(camera.position[0]), 4.0f) && near(camera.position[2], 4.0f),
        "a quarter turn goes round to the side");
    check(near(target[0], 0.0f) && near(target[2], 4.0f), "the camera still faces the pivot");
    RecompFreeCameraControls closer = {0};
    closer.move[2] = 1.0f;
    recomp_free_camera_orbit_step(&camera, &orbit, &closer, 0.0f, 0.5f);
    check(orbit.distance < 4.0f && near(camera.focus, orbit.distance), "forward moves in");
    check(!recomp_free_camera_orbit_begin(&camera, 0.0f, &orbit), "no orbit around the eye");

    /* A path through three views, turning the short way round. */
    RecompCameraPath path = {0};
    RecompFreeCamera key = camera, sampled;
    check(!recomp_camera_path_sample(&path, 0.5f, &sampled), "one view is not a path");
    key.position[0] = 0.0f; key.heading = 3.0f; key.fov = 0.5f;
    recomp_camera_path_add(&path, &key);
    key.position[0] = 10.0f; key.heading = -3.0f; key.fov = 1.0f;
    recomp_camera_path_add(&path, &key);
    key.position[0] = 20.0f;
    recomp_camera_path_add(&path, &key);
    check(recomp_camera_path_sample(&path, 0.0f, &sampled) && near(sampled.position[0], 0.0f), "starts at the first view");
    check(recomp_camera_path_sample(&path, 1.0f, &sampled) && near(sampled.position[0], 20.0f), "ends at the last");
    check(recomp_camera_path_sample(&path, 0.5f, &sampled) && near(sampled.position[0], 10.0f), "passes the middle view");
    recomp_camera_path_sample(&path, 0.25f, &sampled);
    check(fabsf(sampled.heading) > 2.9f, "turns through pi, not through zero");
    check(recomp_camera_path_remove(&path) && path.count == 2u, "removes the last view");

    if (failures == 0) printf("free camera test passed\n");
    return failures == 0 ? 0 : 1;
}
