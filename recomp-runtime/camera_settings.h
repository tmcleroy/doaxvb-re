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
    /* Left, right or A puts every setting back (the free camera does it). */
    RECOMP_CAMERA_SETTING_RESET,
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
    RECOMP_CAMERA_SETTING_TRACK,
    RECOMP_CAMERA_SETTING_HIDE_BALL,
    RECOMP_CAMERA_SETTING_HIDE_OTHERS,
    RECOMP_CAMERA_SETTING_SHOT_SIZE,
    RECOMP_CAMERA_SETTING_TIME_OF_DAY,
    RECOMP_CAMERA_SETTING_LIGHT_ANGLE,
    RECOMP_CAMERA_SETTING_LIGHT_STRENGTH,
    RECOMP_CAMERA_SETTING_RIM_LIGHT,
    RECOMP_CAMERA_SETTING_FILL_LIGHT,
    RECOMP_CAMERA_SETTING_EXPOSURE,
    RECOMP_CAMERA_SETTING_CONTRAST,
    RECOMP_CAMERA_SETTING_SATURATION,
    RECOMP_CAMERA_SETTING_BLOOM,
    RECOMP_CAMERA_SETTING_GRID,
    RECOMP_CAMERA_SETTING_SPOT,
    RECOMP_CAMERA_SETTING_BACKGROUND,
    RECOMP_CAMERA_SETTING_BACKDROP,
    RECOMP_CAMERA_SETTING_PATH,
    RECOMP_CAMERA_SETTING_PATH_LENGTH,
    RECOMP_CAMERA_SETTING_PATH_PLAY,
    RECOMP_CAMERA_SETTING_RECORD,
    RECOMP_CAMERA_SETTING_SPEED,
    RECOMP_CAMERA_SETTING_MOTION_BLUR,
    RECOMP_CAMERA_SETTING_POSE,
    RECOMP_CAMERA_SETTING_MOVE_HER,
    RECOMP_CAMERA_SETTING_EYE_CONTACT,
    RECOMP_CAMERA_SETTING_EYES,
    RECOMP_CAMERA_SETTING_MOUTH,
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

typedef enum RecompCameraGrid {
    RECOMP_CAMERA_GRID_OFF,
    RECOMP_CAMERA_GRID_THIRDS,
    RECOMP_CAMERA_GRID_GOLDEN,
    RECOMP_CAMERA_GRID_CENTRE,
    RECOMP_CAMERA_GRID_COUNT
} RecompCameraGrid;

enum { RECOMP_CAMERA_SPOTS = 4 };

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
    unsigned track;       /* 0 off, 1 the camera aims at her, 2 it also rides along with her */
    bool hide_ball;       /* in a match */
    bool hide_others;     /* every character but the chosen one */
    unsigned shot_size;   /* screenshots at 1 to 4 times the frame, in tiles */
    unsigned time_of_day; /* 0 the stage's own, else half hours from 05:30 (1 is 06:00) */
    int light_angle;      /* degrees the key light turns about the vertical */
    unsigned light_strength; /* tenths of the key light's own strength (10 is its own) */
    unsigned rim_light;   /* 0 off, else a colour lighting her from behind (recomp_camera_settings_rim) */
    unsigned fill_light;  /* tenths of a white light from the camera's side */
    int exposure;         /* quarter stops */
    int contrast;         /* percent added */
    unsigned saturation;  /* tenths (10 is the picture's own) */
    unsigned bloom;       /* tenths */
    unsigned grid;        /* RecompCameraGrid */
    unsigned spot;        /* the camera spot the Camera spot row points at, 0 to 3 */
    bool transparent;     /* screenshots keep only the characters, with a depth picture */
    unsigned backdrop;    /* 0 the scene, else a studio colour behind the characters */
    unsigned path_seconds; /* how long the camera path takes to play, 2 to 60 */
    bool record;          /* playing the path records a video */
    unsigned slow;        /* the scene runs one frame in 1, 2, 4 or 8 (index 0 to 3) */
    unsigned motion_blur; /* 0 off, else each frame averages 2 << n pictures (4, 8, 16) */
    unsigned eye_contact; /* 0 off, 1 head, 2 head and eyes */
    bool move_her;        /* the sticks move and turn the chosen girl, not the camera */
    unsigned eyes;        /* 0 the game's own (blinking), else held (recomp_camera_settings_face) */
    unsigned mouth;       /* 0 the game's own, else held */
} RecompCameraSettings;

void recomp_camera_settings_default(RecompCameraSettings *settings);
/* The eye and mouth shapes held on every girl (body_morph_adapter.h), -1
   for the game's own. */
void recomp_camera_settings_face(const RecompCameraSettings *settings, int *eyes, int *mouth);
/* The rim light's colour, false when it is off. */
bool recomp_camera_settings_rim(const RecompCameraSettings *settings, float rgb[3]);
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
