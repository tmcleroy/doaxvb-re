#include "camera_settings.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const float F_NUMBERS[] = {1.4f, 2.0f, 2.8f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f, 22.0f};
enum { F_NUMBER_COUNT = sizeof F_NUMBERS / sizeof F_NUMBERS[0] };
static const struct { float aspect; const char *name; } LETTERBOXES[] = {
    {0.0f, "Off"}, {2.39f, "2.39:1"}, {1.85f, "1.85:1"}, {16.0f / 9.0f, "16:9"}, {4.0f / 3.0f, "4:3"}, {1.0f, "1:1"}};
enum { LETTERBOX_COUNT = sizeof LETTERBOXES / sizeof LETTERBOXES[0] };
static const float PI = 3.14159265f;
static const float MIN_FOCUS = 0.2f, MAX_FOCUS = 200.0f;
static const float MIN_FOV_DEGREES = 5.0f, MAX_FOV_DEGREES = 120.0f;
static const float MAX_ROLL = 45.0f;
/* Half hours from 06:00 to 22:00. */
static const unsigned TIME_STEPS = 33u;

void recomp_camera_settings_default(RecompCameraSettings *settings)
{
    memset(settings, 0, sizeof *settings);
    settings->focus = RECOMP_CAMERA_FOCUS_CHARACTER;
    settings->focus_distance = 2.5f;
    settings->aperture = 1u; /* f/2 */
    settings->shot_size = 1u;
    settings->light_strength = 10u;
    settings->saturation = 10u;
    settings->path_seconds = 10u;
}

static int wrap(int value, int count)
{
    value %= count;
    return value < 0 ? value + count : value;
}

static unsigned clamp_index(int value, unsigned count)
{
    return value < 0 ? 0u : (unsigned)value >= count ? count - 1u : (unsigned)value;
}

void recomp_camera_settings_change(RecompCameraSettings *settings, RecompCameraSettingRow row,
    int steps, float *fov, float character_focus)
{
    switch (row) {
    case RECOMP_CAMERA_SETTING_DEPTH_OF_FIELD:
        if (steps % 2 != 0) settings->depth_of_field = !settings->depth_of_field;
        break;
    case RECOMP_CAMERA_SETTING_FOCUS:
        settings->focus = (RecompCameraFocus)wrap((int)settings->focus + steps, RECOMP_CAMERA_FOCUS_COUNT);
        /* Manual focus starts where the character is. */
        if (settings->focus == RECOMP_CAMERA_FOCUS_MANUAL && character_focus > 0.0f)
            settings->focus_distance = character_focus;
        break;
    case RECOMP_CAMERA_SETTING_FOCUS_DISTANCE:
        /* Changing the distance makes the focus manual. */
        if (settings->focus != RECOMP_CAMERA_FOCUS_MANUAL && character_focus > 0.0f)
            settings->focus_distance = character_focus;
        settings->focus = RECOMP_CAMERA_FOCUS_MANUAL;
        settings->focus_distance *= powf(1.05f, (float)steps);
        if (settings->focus_distance < MIN_FOCUS) settings->focus_distance = MIN_FOCUS;
        if (settings->focus_distance > MAX_FOCUS) settings->focus_distance = MAX_FOCUS;
        break;
    case RECOMP_CAMERA_SETTING_APERTURE:
        settings->aperture = clamp_index((int)settings->aperture + steps, F_NUMBER_COUNT);
        break;
    case RECOMP_CAMERA_SETTING_FIELD_OF_VIEW:
        if (fov != NULL) {
            float degrees = roundf(*fov * 180.0f / PI) - (float)steps;
            if (degrees < MIN_FOV_DEGREES) degrees = MIN_FOV_DEGREES;
            if (degrees > MAX_FOV_DEGREES) degrees = MAX_FOV_DEGREES;
            *fov = degrees * PI / 180.0f;
        }
        break;
    case RECOMP_CAMERA_SETTING_ROLL:
        settings->roll += (float)steps;
        if (settings->roll < -MAX_ROLL) settings->roll = -MAX_ROLL;
        if (settings->roll > MAX_ROLL) settings->roll = MAX_ROLL;
        break;
    case RECOMP_CAMERA_SETTING_FILTER:
        settings->filter = (RecompCameraFilter)wrap((int)settings->filter + steps, RECOMP_CAMERA_FILTER_COUNT);
        break;
    case RECOMP_CAMERA_SETTING_VIGNETTE:
        settings->vignette = clamp_index((int)settings->vignette + steps, 11u);
        break;
    case RECOMP_CAMERA_SETTING_GRAIN:
        settings->grain = clamp_index((int)settings->grain + steps, 11u);
        break;
    case RECOMP_CAMERA_SETTING_LETTERBOX:
        settings->letterbox = (unsigned)wrap((int)settings->letterbox + steps, LETTERBOX_COUNT);
        break;
    case RECOMP_CAMERA_SETTING_ORBIT:
        settings->orbit = (RecompCameraOrbit)wrap((int)settings->orbit + steps, RECOMP_CAMERA_ORBIT_COUNT);
        break;
    case RECOMP_CAMERA_SETTING_HIDE_BALL:
        if (steps % 2 != 0) settings->hide_ball = !settings->hide_ball;
        break;
    case RECOMP_CAMERA_SETTING_HIDE_OTHERS:
        if (steps % 2 != 0) settings->hide_others = !settings->hide_others;
        break;
    case RECOMP_CAMERA_SETTING_SHOT_SIZE:
        settings->shot_size = 1u + clamp_index((int)settings->shot_size - 1 + steps, 4u);
        break;
    case RECOMP_CAMERA_SETTING_TIME_OF_DAY:
        /* 06:00 to 22:00 in half hours, and the stage's own before them. */
        settings->time_of_day = clamp_index((int)settings->time_of_day + steps, TIME_STEPS + 1u);
        break;
    case RECOMP_CAMERA_SETTING_LIGHT_ANGLE:
        settings->light_angle = wrap(settings->light_angle + 180 + 5 * steps, 360) - 180;
        break;
    case RECOMP_CAMERA_SETTING_LIGHT_STRENGTH:
        /* 10% to 300%: zero would read as no setting at all (camera_effects.h). */
        settings->light_strength = 1u + clamp_index((int)settings->light_strength - 1 + steps, 30u);
        break;
    case RECOMP_CAMERA_SETTING_EXPOSURE:
        settings->exposure = (int)clamp_index(settings->exposure + 8 + steps, 17u) - 8;
        break;
    case RECOMP_CAMERA_SETTING_CONTRAST:
        settings->contrast = 5 * ((int)clamp_index(settings->contrast / 5 + 10 + steps, 21u) - 10);
        break;
    case RECOMP_CAMERA_SETTING_SATURATION:
        settings->saturation = clamp_index((int)settings->saturation + steps, 21u);
        break;
    case RECOMP_CAMERA_SETTING_BLOOM:
        settings->bloom = clamp_index((int)settings->bloom + steps, 11u);
        break;
    case RECOMP_CAMERA_SETTING_GRID:
        settings->grid = (unsigned)wrap((int)settings->grid + steps, RECOMP_CAMERA_GRID_COUNT);
        break;
    case RECOMP_CAMERA_SETTING_SPOT:
        settings->spot = (unsigned)wrap((int)settings->spot + steps, RECOMP_CAMERA_SPOTS);
        break;
    case RECOMP_CAMERA_SETTING_BACKGROUND:
        if (steps % 2 != 0) settings->transparent = !settings->transparent;
        break;
    case RECOMP_CAMERA_SETTING_PATH_LENGTH:
        settings->path_seconds = 2u + clamp_index((int)settings->path_seconds - 2 + steps, 59u);
        break;
    case RECOMP_CAMERA_SETTING_RECORD:
        if (steps % 2 != 0) settings->record = !settings->record;
        break;
    case RECOMP_CAMERA_SETTING_SPEED:
        settings->slow = clamp_index((int)settings->slow + steps, 4u);
        break;
    case RECOMP_CAMERA_SETTING_MOTION_BLUR:
        settings->motion_blur = clamp_index((int)settings->motion_blur + steps, 4u);
        break;
    case RECOMP_CAMERA_SETTING_EYE_CONTACT:
        settings->eye_contact = (unsigned)wrap((int)settings->eye_contact + steps, 3);
        break;
    default:
        break;
    }
}

void recomp_camera_settings_describe(const RecompCameraSettings *settings, RecompCameraSettingRow row,
    float fov, float character_focus, char *text, size_t size)
{
    static const char *const focus_names[RECOMP_CAMERA_FOCUS_COUNT] = {"On her", "Centre of frame", "Manual"};
    switch (row) {
    case RECOMP_CAMERA_SETTING_RESET:
        snprintf(text, size, "Reset all camera options: press right");
        break;
    case RECOMP_CAMERA_SETTING_DEPTH_OF_FIELD:
        snprintf(text, size, "Depth of field: %s", settings->depth_of_field ? "On" : "Off");
        break;
    case RECOMP_CAMERA_SETTING_FOCUS:
        snprintf(text, size, "Focus: %s", focus_names[settings->focus % RECOMP_CAMERA_FOCUS_COUNT]);
        break;
    case RECOMP_CAMERA_SETTING_FOCUS_DISTANCE:
        if (settings->focus == RECOMP_CAMERA_FOCUS_MANUAL)
            snprintf(text, size, "Focus distance: %.2f m", settings->focus_distance);
        else if (settings->focus == RECOMP_CAMERA_FOCUS_CHARACTER && character_focus > 0.0f)
            snprintf(text, size, "Focus distance: %.2f m (on her)", character_focus);
        else
            snprintf(text, size, "Focus distance: automatic");
        break;
    case RECOMP_CAMERA_SETTING_APERTURE:
        snprintf(text, size, "Aperture: f/%g", F_NUMBERS[settings->aperture % F_NUMBER_COUNT]);
        break;
    case RECOMP_CAMERA_SETTING_FIELD_OF_VIEW:
        snprintf(text, size, "Field of view: %.0f deg", fov * 180.0f / PI);
        break;
    case RECOMP_CAMERA_SETTING_ROLL:
        snprintf(text, size, "Roll: %.0f deg", settings->roll);
        break;
    case RECOMP_CAMERA_SETTING_FILTER:
        snprintf(text, size, "Filter: %s", recomp_camera_effects_filter_name(settings->filter));
        break;
    case RECOMP_CAMERA_SETTING_VIGNETTE:
        snprintf(text, size, "Vignette: %u%%", settings->vignette * 10u);
        break;
    case RECOMP_CAMERA_SETTING_GRAIN:
        snprintf(text, size, "Film grain: %u%%", settings->grain * 10u);
        break;
    case RECOMP_CAMERA_SETTING_LETTERBOX:
        snprintf(text, size, "Letterbox: %s", LETTERBOXES[settings->letterbox % LETTERBOX_COUNT].name);
        break;
    case RECOMP_CAMERA_SETTING_ORBIT: {
        static const char *const names[RECOMP_CAMERA_ORBIT_COUNT] = {"Off", "Around the focus", "Turntable"};
        snprintf(text, size, "Orbit: %s", names[settings->orbit % RECOMP_CAMERA_ORBIT_COUNT]);
        break;
    }
    case RECOMP_CAMERA_SETTING_HIDE_BALL:
        snprintf(text, size, "Ball: %s", settings->hide_ball ? "Hidden" : "Shown");
        break;
    case RECOMP_CAMERA_SETTING_HIDE_OTHERS:
        snprintf(text, size, "Other girls: %s", settings->hide_others ? "Hidden" : "Shown");
        break;
    case RECOMP_CAMERA_SETTING_SHOT_SIZE:
        snprintf(text, size, "Screenshot size: %ux", settings->shot_size);
        break;
    case RECOMP_CAMERA_SETTING_LIGHT_ANGLE:
        snprintf(text, size, "Key light angle: %+d deg", settings->light_angle);
        break;
    case RECOMP_CAMERA_SETTING_LIGHT_STRENGTH:
        snprintf(text, size, "Key light strength: %u%%", settings->light_strength * 10u);
        break;
    case RECOMP_CAMERA_SETTING_EXPOSURE:
        snprintf(text, size, "Exposure: %+.2f", (double)settings->exposure / 4.0);
        break;
    case RECOMP_CAMERA_SETTING_CONTRAST:
        snprintf(text, size, "Contrast: %+d%%", settings->contrast);
        break;
    case RECOMP_CAMERA_SETTING_SATURATION:
        snprintf(text, size, "Saturation: %u%%", settings->saturation * 10u);
        break;
    case RECOMP_CAMERA_SETTING_BLOOM:
        snprintf(text, size, "Bloom and glare: %u%%", settings->bloom * 10u);
        break;
    case RECOMP_CAMERA_SETTING_GRID: {
        static const char *const names[RECOMP_CAMERA_GRID_COUNT] = {"Off", "Thirds", "Golden ratio", "Centre"};
        snprintf(text, size, "Grid: %s", names[settings->grid % RECOMP_CAMERA_GRID_COUNT]);
        break;
    }
    case RECOMP_CAMERA_SETTING_PATH:
        snprintf(text, size, "Camera path: right adds this view, left removes the last");
        break;
    case RECOMP_CAMERA_SETTING_PATH_LENGTH:
        snprintf(text, size, "Path length: %u s", settings->path_seconds);
        break;
    case RECOMP_CAMERA_SETTING_PATH_PLAY:
        snprintf(text, size, "Play path: right plays, left stops");
        break;
    case RECOMP_CAMERA_SETTING_RECORD:
        snprintf(text, size, "Record video while the path plays: %s", settings->record ? "On" : "Off");
        break;
    case RECOMP_CAMERA_SETTING_SPEED:
        if (settings->slow == 0u) snprintf(text, size, "Scene speed: normal");
        else snprintf(text, size, "Scene speed: 1/%u (slow motion)", 1u << settings->slow);
        break;
    case RECOMP_CAMERA_SETTING_MOTION_BLUR:
        if (settings->motion_blur == 0u) snprintf(text, size, "Motion blur: Off");
        else snprintf(text, size, "Motion blur: %u pictures a frame", 2u << settings->motion_blur);
        break;
    case RECOMP_CAMERA_SETTING_EYE_CONTACT: {
        static const char *const names[3] = {"Off", "Head", "Head and eyes"};
        snprintf(text, size, "Eye contact: %s", names[settings->eye_contact % 3u]);
        break;
    }
    case RECOMP_CAMERA_SETTING_POSE:
        snprintf(text, size, "Pose: left steps back, right forward (, and . keys)");
        break;
    case RECOMP_CAMERA_SETTING_BACKGROUND:
        snprintf(text, size, "Screenshot background: %s",
            settings->transparent ? "Transparent, girls only (+ depth)" : "The scene");
        break;
    case RECOMP_CAMERA_SETTING_SPOT:
        snprintf(text, size, "Camera spot %u: A go, X save (keys 1-4, Shift saves)", settings->spot + 1u);
        break;
    case RECOMP_CAMERA_SETTING_TIME_OF_DAY:
        if (settings->time_of_day == 0u) {
            snprintf(text, size, "Time of day: the stage's own");
        } else {
            const unsigned minutes = 330u + 30u * settings->time_of_day;
            snprintf(text, size, "Time of day: %02u:%02u", minutes / 60u, minutes % 60u);
        }
        break;
    default:
        snprintf(text, size, "?");
        break;
    }
}

void recomp_camera_settings_effects(const RecompCameraSettings *settings, float fov,
    float character_focus, RecompCameraEffects *effects)
{
    memset(effects, 0, sizeof *effects);
    effects->depth_of_field = settings->depth_of_field;
    (void)character_focus;
    effects->focus = settings->focus == RECOMP_CAMERA_FOCUS_MANUAL ? settings->focus_distance
        : settings->focus == RECOMP_CAMERA_FOCUS_CHARACTER ? RECOMP_CAMERA_FOCUS_SUBJECT : 0.0f;
    effects->aperture = F_NUMBERS[settings->aperture % F_NUMBER_COUNT];
    effects->fov = fov;
    effects->roll = settings->roll * PI / 180.0f;
    effects->filter = settings->filter;
    effects->vignette = (float)settings->vignette / 10.0f;
    effects->grain = (float)settings->grain / 10.0f;
    effects->letterbox = LETTERBOXES[settings->letterbox % LETTERBOX_COUNT].aspect;
    effects->hour = settings->time_of_day == 0u ? -1.0f : 5.5f + 0.5f * (float)settings->time_of_day;
    effects->light_angle = (float)settings->light_angle * PI / 180.0f;
    effects->light_strength = (float)settings->light_strength / 10.0f;
    effects->exposure = (float)settings->exposure / 4.0f;
    effects->contrast = (float)settings->contrast / 100.0f;
    effects->saturation = (float)settings->saturation / 10.0f;
    effects->bloom = (float)settings->bloom / 10.0f;
    effects->transparent = settings->transparent;
    effects->active = effects->depth_of_field || settings->roll != 0.0f ||
        settings->filter != RECOMP_CAMERA_FILTER_NONE || settings->vignette != 0u ||
        settings->grain != 0u || effects->letterbox > 0.0f || settings->exposure != 0 ||
        settings->contrast != 0 || settings->saturation != 10u || settings->bloom != 0u ||
        settings->transparent;
}
