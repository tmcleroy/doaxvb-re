#ifndef DOAXBV_RECOMP_SLOW_MOTION_H
#define DOAXBV_RECOMP_SLOW_MOTION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Smooth slow motion for the free camera (docs/free-camera.md): the scene
   between its last two game frames, shown in the frames between steps. */

/* Samples the scene as the step just run left it; the sample before becomes
   the one blended from. Not `continuous` (the first step, or after anything
   else moved the scene) blends from the new sample itself. */
void recomp_slow_motion_commit(bool continuous);
/* Shows the scene `alpha` of the way from the sample before to the newest.
   Characters in `skip` (bit n for character n) are left as they are. */
void recomp_slow_motion_apply(float alpha, uint32_t skip);
/* Puts the newest sample back exactly, before the scene runs on. */
void recomp_slow_motion_restore(void);
/* True while a blend is shown. */
bool recomp_slow_motion_blended(void);
/* Forgets the samples. */
void recomp_slow_motion_reset(void);

#ifdef __cplusplus
}
#endif

#endif
