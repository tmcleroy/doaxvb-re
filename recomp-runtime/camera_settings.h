#ifndef DOAXBV_RECOMP_CAMERA_SETTINGS_H
#define DOAXBV_RECOMP_CAMERA_SETTINGS_H

#include "camera_effects.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The free camera's settings list (Tab, or BACK on a controller): one row
   per setting, each moved through with left and right. Plain data and rules
   only; free_camera_adapter.c reads the keys and draws the list. */
typedef enum RecompCameraSettingRow {
    RECOMP_CAMERA_SETTING_DEPTH_OF_FIELD,
    RECOMP_CAMERA_SETTING_FOCUS,
    RECOMP_CAMERA_SETTING_FOCUS_DISTANCE,
    RECOMP_CAMERA_SETTING_APERTURE,
    RECOMP_CAMERA_SETTING_FIELD_OF_VIEW,
    RECOMP_CAMERA_SETTING_ROLL,
    RECOMP_CAMERA_SETTING_FILTER,
    RECOMP_CAMERA_SETTING_VIGNETTE,
    RECOMP_CAMERA_SETTING_GRAIN,
    RECOMP_CAMERA_SETTING_LETTERBOX,
    RECOMP_CAMERA_SETTING_ORBIT,
    RECOMP_CAMERA_SETTING_HIDE_BALL,
    RECOMP_CAMERA_SETTING_HIDE_OTHERS,
    RECOMP_CAMERA_SETTING_SHOT_SIZE,
    RECOMP_CAMERA_SETTING_COUNT
} RecompCameraSettingRow;

typedef enum RecompCameraFocus {
    /* On the chosen character, wherever she moves. */
    RECOMP_CAMERA_FOCUS_CHARACTER,
    /* On whatever is at the centre of the frame. */
    RECOMP_CAMERA_FOCUS_CENTRE,
    RECOMP_CAMERA_FOCUS_MANUAL,
    RECOMP_CAMERA_FOCUS_COUNT
} RecompCameraFocus;

typedef enum RecompCameraOrbit {
    RECOMP_CAMERA_ORBIT_OFF,
    /* The sticks go round the point in focus instead of flying. */
    RECOMP_CAMERA_ORBIT_MANUAL,
    /* As manual, turning slowly by itself. */
    RECOMP_CAMERA_ORBIT_TURNTABLE,
    RECOMP_CAMERA_ORBIT_COUNT
} RecompCameraOrbit;

typedef struct RecompCameraSettings {
    bool depth_of_field;
    RecompCameraFocus focus;
    /* World units, for manual focus. */
    float focus_distance;
    unsigned aperture;    /* index into the f-numbers */
    float roll;           /* degrees */
    RecompCameraFilter filter;
    unsigned vignette;    /* tenths */
    unsigned grain;       /* tenths */
    unsigned letterbox;   /* index into the aspect ratios */
    RecompCameraOrbit orbit;
    bool hide_ball;       /* in a match */
    bool hide_others;     /* every character but the chosen one */
    unsigned shot_size;   /* screenshots at 1 to 4 times the frame, in tiles */
} RecompCameraSettings;

void recomp_camera_settings_default(RecompCameraSettings *settings);
/* Moves a row by `steps` (negative goes back). The field of view row changes
   *fov, in radians. `character_focus` is the distance to the chosen
   character, or zero when there is none; manual focus starts from it. */
void recomp_camera_settings_change(RecompCameraSettings *settings, RecompCameraSettingRow row,
    int steps, float *fov, float character_focus);
/* "Name: value" for the list. */
void recomp_camera_settings_describe(const RecompCameraSettings *settings, RecompCameraSettingRow row,
    float fov, float character_focus, char *text, size_t size);
/* What the presenter draws for these settings, through a camera of this
   field of view. */
void recomp_camera_settings_effects(const RecompCameraSettings *settings, float fov,
    float character_focus, RecompCameraEffects *effects);

#ifdef __cplusplus
}
#endif

#endif
