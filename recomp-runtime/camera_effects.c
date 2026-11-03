#include "camera_effects.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <stdatomic.h>
#endif

float recomp_camera_effects_blur_scale(const RecompCameraEffects *effects, float focus, float height)
{
    if (effects == NULL || !(effects->aperture > 0.0f) || !(effects->fov > 0.0f) ||
        !(effects->fov < 3.1f) || !(focus > 0.0f) || !(height > 0.0f)) return 0.0f;
    /* Thin lens on a 24 mm tall sensor: the focal length that gives this
       field of view, and the blur circle on the sensor for a point at depth
       d is f^2 / (N (focus - f)) * |d - focus| / d. */
    const float sensor = 0.024f;
    const float length = 0.5f * sensor / tanf(0.5f * effects->fov);
    const float plane = focus > length * 1.01f ? focus : length * 1.01f;
    const float scale = length * length / (effects->aperture * (plane - length)) / sensor * height;
    return isfinite(scale) ? scale : 0.0f;
}

static void set_saturation(float matrix[3][4], float saturation, float contrast, float lift)
{
    static const float luma[3] = {0.2126f, 0.7152f, 0.0722f};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            float value = (1.0f - saturation) * luma[column] + (row == column ? saturation : 0.0f);
            matrix[row][column] = value * contrast;
        }
        matrix[row][3] = 0.5f * (1.0f - contrast) + lift;
    }
}

void recomp_camera_effects_filter_matrix(RecompCameraFilter filter, float matrix[3][4])
{
    memset(matrix, 0, sizeof(float) * 12u);
    for (int i = 0; i < 3; ++i) matrix[i][i] = 1.0f;
    switch (filter) {
    case RECOMP_CAMERA_FILTER_WARM:
        matrix[0][0] = 1.08f; matrix[0][3] = 0.02f;
        matrix[2][2] = 0.86f;
        break;
    case RECOMP_CAMERA_FILTER_COOL:
        matrix[0][0] = 0.90f;
        matrix[2][2] = 1.10f; matrix[2][3] = 0.02f;
        break;
    case RECOMP_CAMERA_FILTER_VIVID: set_saturation(matrix, 1.35f, 1.08f, 0.0f); break;
    case RECOMP_CAMERA_FILTER_MUTED: set_saturation(matrix, 0.6f, 0.92f, 0.03f); break;
    case RECOMP_CAMERA_FILTER_MONO: set_saturation(matrix, 0.0f, 1.05f, 0.0f); break;
    case RECOMP_CAMERA_FILTER_SEPIA: {
        static const float sepia[3][3] = {
            {0.393f, 0.769f, 0.189f}, {0.349f, 0.686f, 0.168f}, {0.272f, 0.534f, 0.131f}};
        for (int row = 0; row < 3; ++row) memcpy(matrix[row], sepia[row], sizeof sepia[row]);
        break;
    }
    default: break;
    }
}

const char *recomp_camera_effects_filter_name(RecompCameraFilter filter)
{
    static const char *const names[RECOMP_CAMERA_FILTER_COUNT] = {
        "None", "Warm", "Cool", "Vivid", "Muted", "Black and white", "Sepia"};
    return (unsigned)filter < RECOMP_CAMERA_FILTER_COUNT ? names[filter] : names[0];
}

void recomp_camera_effects_adjust_matrix(const RecompCameraEffects *effects, float matrix[3][4])
{
    static const float luma[3] = {0.2126f, 0.7152f, 0.0722f};
    const float gain = exp2f(effects->exposure);
    const float saturation = effects->saturation > 0.0f ? effects->saturation : 1.0f;
    const float contrast = 1.0f + effects->contrast;
    float adjust[3][4];
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const float mixed = (1.0f - saturation) * luma[column] + (row == column ? saturation : 0.0f);
            adjust[row][column] = contrast * gain * mixed;
        }
        adjust[row][3] = 0.5f * (1.0f - contrast);
    }
    float out[3][4];
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column) {
            float sum = column == 3 ? adjust[row][3] : 0.0f;
            for (int k = 0; k < 3; ++k) sum += adjust[row][k] * matrix[k][column];
            out[row][column] = sum;
        }
    memcpy(matrix, out, sizeof out);
}

/* The same handover as overlay_text.c: a spin lock held only for the copy. */
static RecompCameraEffects published;
#ifdef _WIN32
static volatile LONG published_lock;
static void lock(void) { while (InterlockedExchange(&published_lock, 1) != 0) {} }
static void unlock(void) { InterlockedExchange(&published_lock, 0); }
#else
static atomic_flag published_lock = ATOMIC_FLAG_INIT;
static void lock(void) { while (atomic_flag_test_and_set_explicit(&published_lock, memory_order_acquire)) {} }
static void unlock(void) { atomic_flag_clear_explicit(&published_lock, memory_order_release); }
#endif

void recomp_camera_effects_publish(const RecompCameraEffects *effects)
{
    if (effects == NULL) return;
    lock();
    published = *effects;
    unlock();
}

/* A launcher amount, 0 to 1 (a missing or unreadable one is 0). */
static float launcher_amount(const char *name)
{
    const char *text = getenv(name);
    const float value = text != NULL ? strtof(text, NULL) : 0.0f;
    return value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f;
}

void recomp_camera_effects_always(float *occlusion, float *bloom)
{
    /* Read once: the environment is set before the runner starts. */
    static int read;
    static float kept_occlusion, kept_bloom;
    if (!read) {
        kept_occlusion = launcher_amount("RECOMP_D3D_OCCLUSION");
        kept_bloom = launcher_amount("RECOMP_D3D_BLOOM");
        read = 1;
    }
    if (occlusion != NULL) *occlusion = kept_occlusion;
    if (bloom != NULL) *bloom = kept_bloom;
}

bool recomp_camera_effects_always_wanted(void)
{
    float occlusion, bloom;
    recomp_camera_effects_always(&occlusion, &bloom);
    return occlusion > 0.0f || bloom > 0.0f;
}

void recomp_camera_effects_neutral(RecompCameraEffects *effects)
{
    if (effects == NULL) return;
    memset(effects, 0, sizeof *effects);
    effects->aperture = 2.8f;
    effects->hour = -1.0f;
    effects->light_strength = 1.0f;
    effects->saturation = 1.0f;
}

void recomp_camera_effects_take(RecompCameraEffects *effects)
{
    if (effects == NULL) return;
    lock();
    *effects = published;
    unlock();
    /* The launcher's own: in every scene, and while the camera flies the
       stronger of its value and the camera's. */
    float occlusion, bloom;
    recomp_camera_effects_always(&occlusion, &bloom);
    if (!(occlusion > 0.0f || bloom > 0.0f)) return;
    if (!effects->active) {
        /* The camera's look is not shown: only its lens, light, background
           and recording settings stay, which others read. */
        effects->depth_of_field = false;
        effects->filter = RECOMP_CAMERA_FILTER_NONE;
        effects->vignette = effects->grain = effects->letterbox = 0.0f;
        effects->exposure = effects->contrast = 0.0f;
        effects->saturation = 1.0f;
        effects->bloom = effects->occlusion = 0.0f;
    }
    if (occlusion > effects->occlusion) effects->occlusion = occlusion;
    if (bloom > effects->bloom) effects->bloom = bloom;
    effects->active = true;
}

static float subject;

void recomp_camera_effects_report_subject(float depth)
{
    lock();
    subject = depth > 0.0f && isfinite(depth) ? depth : 0.0f;
    unlock();
}

static bool scene_tinted;
static float scene_tint[3] = {1.0f, 1.0f, 1.0f};

void recomp_camera_effects_report_scene_tint(bool on, const float tint[3])
{
    lock();
    scene_tinted = on;
    if (on && tint != NULL) memcpy(scene_tint, tint, sizeof scene_tint);
    unlock();
}

bool recomp_camera_effects_scene_tint(float tint[3])
{
    lock();
    const bool on = scene_tinted;
    memcpy(tint, scene_tint, sizeof scene_tint);
    unlock();
    return on;
}

static unsigned big_shot;

void recomp_camera_effects_request_big_shot(unsigned scale)
{
    lock();
    big_shot = scale;
    unlock();
}

unsigned recomp_camera_effects_take_big_shot(void)
{
    lock();
    const unsigned scale = big_shot;
    big_shot = 0u;
    unlock();
    return scale;
}

float recomp_camera_effects_subject(void)
{
    lock();
    float depth = subject;
    unlock();
    return depth;
}
