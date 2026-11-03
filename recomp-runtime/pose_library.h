#ifndef DOAXBV_RECOMP_POSE_LIBRARY_H
#define DOAXBV_RECOMP_POSE_LIBRARY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Saved poses for the free camera (docs/free-camera.md): a character's 32
   bone matrices (rows: three axes, then the place) kept relative to where
   she stands and which way she faces (her hips' place on the ground and
   their facing, level), so the pose, with its own height and tilt, can be
   put back wherever she stands. Bones carry her
   own proportions, so a pose is put back only on a character of the same
   type. Plain data and arithmetic, nothing of the guest. */
enum { RECOMP_POSE_BONES = 32, RECOMP_POSE_SLOTS = 8 };

typedef struct RecompPoseSlot {
    bool used;
    uint8_t type;   /* the character's type (the byte at +1 of her state record) */
    float bones[RECOMP_POSE_BONES][16];
} RecompPoseSlot;

/* Keeps `bones` (world matrices; a bone with [15] zero is unused) relative
   to where bone 0 stands. False when bone 0 faces straight up or down. */
bool recomp_pose_keep(const float bones[RECOMP_POSE_BONES][16], uint8_t type, RecompPoseSlot *slot);
/* Puts the kept pose back where `hips` (her bone 0 now) stands, facing its
   way: every used bone, the hips too. */
void recomp_pose_put_back(const RecompPoseSlot *slot, const float hips[16], float bones[RECOMP_POSE_BONES][16]);

#ifdef __cplusplus
}
#endif

#endif
