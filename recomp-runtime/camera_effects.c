#include "camera_effects.h"

#include <math.h>
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

void recomp_camera_effects_take(RecompCameraEffects *effects)
{
    if (effects == NULL) return;
    lock();
    *effects = published;
    unlock();
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
