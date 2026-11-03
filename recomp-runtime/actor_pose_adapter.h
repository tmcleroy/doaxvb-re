#ifndef DOAXBV_RECOMP_ACTOR_POSE_ADAPTER_H
#define DOAXBV_RECOMP_ACTOR_POSE_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

#include "pose_library.h"

#include "runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Picks the next active character to pose. */
void recomp_actor_pose_next_actor(void);
/* What the pose controls are doing, for the overlay. */
typedef struct RecompActorPoseStatus {
    uint32_t actor;
    bool active;
    bool holding;
    uint16_t clip;
    /* The held clip's place in the list of clips, from 1, and the list's
       length; zero when nothing is held. */
    uint32_t position, count;
    /* A clip being looked for and not yet played, with its place and the
       list's length. */
    bool browsing;
    uint16_t browse_clip;
    uint32_t browse_position, browse_count;
} RecompActorPoseStatus;
void recomp_actor_pose_status(RecompActorPoseStatus *status);
/* The character's name when the game names her (in a match), else
   "Character n". */
void recomp_actor_pose_name(uint32_t actor, char *name, uint32_t size);
/* Chooses a character to pose by number. */
void recomp_actor_pose_choose(uint32_t actor);
/* Whether a draw's WORLD matrix is one of the palettes built this frame
   from a character's bones: true only for the characters' own pieces. */
bool recomp_actor_pose_character_world(const float world[16]);
/* With eye contact on in a frozen scene, builds the characters' skeletons
   again so they look at the camera; once at the start of a frozen frame. */
void recomp_actor_pose_look_while_frozen(void);
/* Moves the chosen character along the ground (`right` and `forward` in
   world units, relative to a camera of this heading) and turns her by `turn`
   radians about her hips; only her drawn skeleton moves, not the game's
   idea of where she is. */
void recomp_actor_pose_place(float right, float forward, float heading, float turn);
/* The pose library (pose_library.h): keeps the chosen character's pose in
   a slot (false when she is not in the scene); holds a slot's pose on her
   (or lets it go), which only a character of the pose's type takes; the
   slot she holds, or -1. The library as a whole, for keeping between runs. */
bool recomp_actor_pose_keep_pose(unsigned slot);
void recomp_actor_pose_hold_pose(unsigned slot, bool hold);
int recomp_actor_pose_held_pose(void);
const RecompPoseSlot *recomp_actor_pose_library(void);
void recomp_actor_pose_set_library(const RecompPoseSlot library[RECOMP_POSE_SLOTS]);
/* The type of the chosen character (the byte at +1 of her state record), or 0. */
unsigned recomp_actor_pose_chosen_type(void);
/* Puts the chosen character (or all) back where the game has her. */
void recomp_actor_pose_place_reset(bool all);
/* Characters whose bit is set are moved far below the ground. */
void recomp_actor_pose_set_hidden(uint32_t mask);
/* Where the chosen character's hips are, for the camera to face. */
bool recomp_actor_pose_focus(float point[3]);
/* Where character `actor`'s hips are, without choosing her; false when she
   is not in the scene or is hidden. */
bool recomp_actor_pose_hips(uint32_t actor, float point[3]);
/* The level direction the chosen character's hips face. */
bool recomp_actor_pose_facing(float direction[3]);
/* Steps back (step < 0) or forward through the characters' poses of the
   last 600 frames while the scene is frozen, putting their bones back;
   returns how many frames back the shown pose is (0 the newest). */
unsigned recomp_actor_pose_history_step(int step);
/* Eye contact: while on, every character's head (and with `eyes` her eyes)
   turns toward `eye`, the camera, as the game turns them toward the ball. */
void recomp_actor_pose_set_look(bool on, bool eyes, const float eye[3]);
unsigned recomp_actor_pose_history_back(void);
/* Plays the next (step > 0) or previous clip on the chosen character and
   holds it. True when a clip was requested. */
bool recomp_actor_pose_step_clip(int step);
/* Moves the clip being looked for `step` places through the list without
   playing it, starting from the one playing. True when there is a list. */
bool recomp_actor_pose_browse(int step);
/* Plays and holds the clip being looked for. True when one was. */
bool recomp_actor_pose_play_browsed(void);
/* Hands the chosen character's animation back to the game. */
void recomp_actor_pose_release(void);
/* Once a frame, after the game's animation update: keeps a held clip playing
   and the character where it stood. */
void recomp_actor_pose_frame(void);
/* Wraps the skeleton build to keep a held character in place. */
RecompFunction recomp_actor_pose_lookup_manual(uint32_t guest_address);
/* Only what recomp_actor_pose_character_world needs (the palette build it
   watches), for draws marked as a character's without the free camera. */
RecompFunction recomp_actor_pose_lookup_character(uint32_t guest_address);

#ifdef __cplusplus
}
#endif

#endif
