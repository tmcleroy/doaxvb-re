#ifndef DOAXBV_RECOMP_TIME_OF_DAY_H
#define DOAXBV_RECOMP_TIME_OF_DAY_H

#include "stage_lights.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Time of day for the free camera (docs/time-of-day.md). Each stage is made
   for one time, its lights and its baked colours alike. A look describes a
   time: the key light (scene light 0) and its ambient colour, how strong the
   fill lights are, how high the key light stands, and the colour the baked
   scene takes. Looks for the hours between keyframes are blended; the
   keyframes come from the game's own day, sunset and dusk stages. */
typedef struct RecompTimeOfDayLook {
    float key[3];       /* light 0's diffuse colour */
    float ambient[3];   /* light 0's ambient colour, the scene's ambient */
    float fill;         /* lights 1 to 3, relative to day */
    float elevation;    /* radians above the horizon of the key light */
    float tint[3];      /* the baked scene's colour, relative to day */
} RecompTimeOfDayLook;

enum { RECOMP_TIME_OF_DAY_KEYS = 6 };

/* The look at `hour` (0 to 24), blended between keyframes. */
void recomp_time_of_day_look(float hour, RecompTimeOfDayLook *look);
/* The hour of the game's own times: 0 day, 1 sunset, 2 dusk. */
float recomp_time_of_day_game_hour(unsigned time);
/* Changes a scene light record from the stage's look to the target's: the
   key light takes the target's colours, the fills are scaled. */
void recomp_time_of_day_relight(const RecompTimeOfDayLook *stage, const RecompTimeOfDayLook *target,
    unsigned slot, RecompStageLight *light);
/* The key light's direction toward the light at the target's elevation,
   keeping the compass heading of `toward`. */
void recomp_time_of_day_direction(const float toward[3], float elevation, float out[3]);
/* The baked scene's tint: the target's colour over the stage's. */
void recomp_time_of_day_tint(const RecompTimeOfDayLook *stage, const RecompTimeOfDayLook *target, float tint[3]);

#ifdef __cplusplus
}
#endif

#endif
