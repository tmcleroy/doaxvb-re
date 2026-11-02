#ifndef DOAXBV_RECOMP_CAMERA_PATH_H
#define DOAXBV_RECOMP_CAMERA_PATH_H

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
    unsigned count;
} RecompCameraPath;

/* Adds a view at the end; false when the path is full. */
bool recomp_camera_path_add(RecompCameraPath *path, const RecompFreeCamera *view);
/* Removes the last view; false when the path is empty. */
bool recomp_camera_path_remove(RecompCameraPath *path);
/* The view at `t` from 0 (the first key) to 1 (the last): a Catmull-Rom
   curve through the keys' positions, headings (the short way round),
   pitches, fields of view and focus distances, eased in and out. False
   with fewer than two keys. */
bool recomp_camera_path_sample(const RecompCameraPath *path, float t, RecompFreeCamera *view);

#ifdef __cplusplus
}
#endif

#endif
