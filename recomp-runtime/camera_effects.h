#ifndef DOAXBV_RECOMP_CAMERA_EFFECTS_H
#define DOAXBV_RECOMP_CAMERA_EFFECTS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Photo effects the free camera asks a presenter to apply to the finished
   frame: depth of field, roll, a colour filter, vignette, grain and
   letterbox bars. Plain data: free_camera_adapter.c fills it once a frame
   and a presenter, possibly on its own thread, takes the latest copy. Only
   the window and screenshots show the effects; the game's own copies of the
   frame stay as it drew them. */
typedef enum RecompCameraFilter {
    RECOMP_CAMERA_FILTER_NONE,
    RECOMP_CAMERA_FILTER_WARM,
    RECOMP_CAMERA_FILTER_COOL,
    RECOMP_CAMERA_FILTER_VIVID,
    RECOMP_CAMERA_FILTER_MUTED,
    RECOMP_CAMERA_FILTER_MONO,
    RECOMP_CAMERA_FILTER_SEPIA,
    RECOMP_CAMERA_FILTER_COUNT
} RecompCameraFilter;

typedef struct RecompCameraEffects {
    /* False: the presenter shows the frame untouched. */
    bool active;
    bool depth_of_field;
    /* World units from the eye to the plane in focus. Zero focuses on what
       is at the centre of the frame, and RECOMP_CAMERA_FOCUS_SUBJECT on the
       character nearest the middle of the picture. */
    float focus;
    /* Lens f-number: smaller blurs more. */
    float aperture;
    /* Vertical field of view in radians; with the aperture it sets the
       blur, as a lens of that angle on a full-frame camera would. */
    float fov;
    /* Radians, positive turning the picture anticlockwise. The free camera
       turns its view; presenters ignore it. */
    float roll;
    RecompCameraFilter filter;
    /* 0 to 1. */
    float vignette, grain;
    /* Width over height of the picture between black bars; zero for none. */
    float letterbox;
    /* Time of day in hours (time_of_day.h), or below zero for the stage's
       own; the scene lights take it while the camera flies. */
    float hour;
    /* The key light (scene light 0): radians turned about the vertical, and
       a multiple of its own strength. 0 and 1 leave it. */
    float light_angle, light_strength;
    /* Stops brighter, contrast added (-0.5 to 0.5), saturation multiple. */
    float exposure, contrast, saturation;
    /* Bloom and sun glare, 0 to 1. */
    float bloom;
    /* Ambient occlusion, 0 to 1: creases darken (needs the view depth). */
    float occlusion;
    /* Screenshots keep only the characters (alpha from the presenter's
       character mask) and save a depth picture beside them. */
    bool transparent;
    /* A studio backdrop: when set, only the characters are drawn, over this
       colour, in place of the scene. */
    bool backdrop;
    float backdrop_rgb[3];
    /* Every frame shown goes to a video file while this is set. */
    bool recording;
} RecompCameraEffects;

/* Blur, in pixels of a frame `height` pixels tall, of a point `depth` world
   units from the eye: the circle of confusion is scale * |depth - focus| /
   depth. A world unit is taken as a metre and the sensor as 24 mm tall. */
float recomp_camera_effects_blur_scale(const RecompCameraEffects *effects, float focus, float height);
/* The filter's colour matrix: rgb' = m[0..2]·rgb + m[3], one row per channel. */
void recomp_camera_effects_filter_matrix(RecompCameraFilter filter, float matrix[3][4]);
const char *recomp_camera_effects_filter_name(RecompCameraFilter filter);
/* Applies exposure, then saturation, then contrast after a filter matrix. */
void recomp_camera_effects_adjust_matrix(const RecompCameraEffects *effects, float matrix[3][4]);

#define RECOMP_CAMERA_FOCUS_SUBJECT (-1.0f)

void recomp_camera_effects_publish(const RecompCameraEffects *effects);
/* The latest effects, with the launcher's always-on ones (below) added. */
void recomp_camera_effects_take(RecompCameraEffects *effects);
/* Graphics options the launcher sets that use the same effects pass in
   every scene, free camera or not: RECOMP_D3D_OCCLUSION (ambient
   occlusion) and RECOMP_D3D_BLOOM (bloom and sun glare), each 0 to 1. While
   the camera flies, the stronger of these and its own rows is used. */
void recomp_camera_effects_always(float *occlusion, float *bloom);
bool recomp_camera_effects_always_wanted(void);
/* Effects that change nothing: no filter, the stage's own hour and light. */
void recomp_camera_effects_neutral(RecompCameraEffects *effects);
/* The presenter reports how far ahead the character nearest the middle of
   the last frame was (zero for none), for the free camera to show. */
void recomp_camera_effects_report_subject(float depth);
float recomp_camera_effects_subject(void);
/* The scene lights report the tint the baked scene takes for the time of day
   (draws the game does not light: the stage and the sky); off, the
   presenter leaves them as drawn. */
void recomp_camera_effects_report_scene_tint(bool on, const float tint[3]);
bool recomp_camera_effects_scene_tint(float tint[3]);
/* A big screenshot: the presenter draws the next frame `scale` times wider
   and taller and saves it. Taking the request clears it; 0 for none. */
void recomp_camera_effects_request_big_shot(unsigned scale);
unsigned recomp_camera_effects_take_big_shot(void);

#ifdef __cplusplus
}
#endif

#endif
