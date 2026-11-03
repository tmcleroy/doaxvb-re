#ifndef DOAXBV_RECOMP_CAMERA_PATH_H
#define DOAXBV_RECOMP_CAMERA_PATH_H

#include "camera_effects.h"
#include "free_camera.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A camera path for the free camera: views saved as keyframes, played back
   along a smooth curve through them (docs/free-camera.md). */
enum { RECOMP_CAMERA_PATH_KEYS = 16 };

typedef struct RecompCameraPath {
    RecompFreeCamera keys[RECOMP_CAMERA_PATH_KEYS];
    /* Each view's look (the camera settings' effects when it was added). */
    RecompCameraEffects looks[RECOMP_CAMERA_PATH_KEYS];
    bool looked[RECOMP_CAMERA_PATH_KEYS];
    unsigned count;
} RecompCameraPath;

/* Adds a view at the end; false when the path is full. */
bool recomp_camera_path_add(RecompCameraPath *path, const RecompFreeCamera *view);
/* Adds a view with its look. */
bool recomp_camera_path_add_look(RecompCameraPath *path, const RecompFreeCamera *view,
    const RecompCameraEffects *look);
/* Removes the last view; false when the path is empty. */
bool recomp_camera_path_remove(RecompCameraPath *path);
/* The view at `t` from 0 (the first key) to 1 (the last): a Catmull-Rom
   curve through the keys' positions, headings (the short way round),
   pitches, fields of view and focus distances, eased in and out. False
   with fewer than two keys. */
bool recomp_camera_path_sample(const RecompCameraPath *path, float t, RecompFreeCamera *view);
/* The look at `t`, on the same eased timing: amounts (exposure, contrast,
   saturation, bloom, vignette, grain, roll, aperture, the key light, a
   manual focus, and the hour when both views set one) blend from one view to
   the next; choices (filter, depth of field on or off, letterbox, the
   stage's own hour) change halfway. False unless every view has a look. */
bool recomp_camera_path_sample_look(const RecompCameraPath *path, float t, RecompCameraEffects *look);

#ifdef __cplusplus
}
#endif

#endif
