#ifndef DOAXBV_RECOMP_CAMERA_STORE_H
#define DOAXBV_RECOMP_CAMERA_STORE_H

#include "camera_path.h"
#include "camera_settings.h"
#include "free_camera.h"
#include "pose_library.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the free camera keeps between runs (docs/free-camera.md): its
   settings, the four camera spots, the camera path with each view's look,
   and the pose library. Kept as text, a line a value ("name value..."), so
   a file from an older or newer runner reads what it knows and skips the
   rest. Moving a
   girl and recording are not kept: a run never starts doing either. */
typedef struct RecompCameraStore {
    RecompCameraSettings settings;
    RecompFreeCamera spots[RECOMP_CAMERA_SPOTS];
    bool spot_saved[RECOMP_CAMERA_SPOTS];
    RecompCameraPath path;
    /* The pose library. */
    RecompPoseSlot poses[RECOMP_POSE_SLOTS];
} RecompCameraStore;

/* The store as text; the length written, or 0 when it does not fit. */
size_t recomp_camera_store_write(const RecompCameraStore *store, char *text, size_t size);
/* Reads what the text holds into `store`, leaving the rest as it was; the
   settings it reads are brought into their ranges. */
void recomp_camera_store_read(const char *text, RecompCameraStore *store);

/* `.recomp-storage/free-camera-v1.txt` under the game's folder. Save writes
   a new file and puts it in place; false when it could not. */
bool recomp_camera_store_load(const char *root, RecompCameraStore *store);
bool recomp_camera_store_save(const char *root, const RecompCameraStore *store);

#ifdef __cplusplus
}
#endif

#endif
