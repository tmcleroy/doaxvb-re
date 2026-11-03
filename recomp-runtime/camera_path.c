#include "camera_path.h"

#include <math.h>
#include <string.h>

static const float PI = 3.14159265f;

bool recomp_camera_path_add_look(RecompCameraPath *path, const RecompFreeCamera *view,
    const RecompCameraEffects *look)
{
    if (path == NULL || view == NULL || path->count >= RECOMP_CAMERA_PATH_KEYS) return false;
    path->keys[path->count] = *view;
    path->looked[path->count] = look != NULL;
    if (look != NULL) path->looks[path->count] = *look;
    else memset(&path->looks[path->count], 0, sizeof path->looks[0]);
    ++path->count;
    return true;
}

bool recomp_camera_path_add(RecompCameraPath *path, const RecompFreeCamera *view)
{
    return recomp_camera_path_add_look(path, view, NULL);
}

bool recomp_camera_path_remove(RecompCameraPath *path)
{
    if (path == NULL || path->count == 0u) return false;
    --path->count;
    return true;
}

/* Catmull-Rom between b and c, with a and d their neighbours. */
static float spline(float a, float b, float c, float d, float t)
{
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * (2.0f * b + (c - a) * t + (2.0f * a - 5.0f * b + 4.0f * c - d) * t2 +
        (3.0f * b - a - 3.0f * c + d) * t3);
}

/* Headings unwrapped so each key is within half a turn of the last. */
static void unwrapped_headings(const RecompCameraPath *path, float out[RECOMP_CAMERA_PATH_KEYS])
{
    out[0] = path->keys[0].heading;
    for (unsigned i = 1u; i < path->count; ++i) {
        float step = remainderf(path->keys[i].heading - path->keys[i - 1u].heading, 2.0f * PI);
        out[i] = out[i - 1u] + step;
    }
}

/* The span (from key `span` to the next) at `t`, eased over the whole path,
   and how far along it. */
static unsigned locate(const RecompCameraPath *path, float t, float *u)
{
    if (!(t > 0.0f)) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    /* Ease in and out over the whole path. */
    t = t * t * (3.0f - 2.0f * t);
    const unsigned spans = path->count - 1u;
    float at = t * (float)spans;
    unsigned span = (unsigned)at;
    if (span >= spans) span = spans - 1u;
    *u = at - (float)span;
    return span;
}

static float blend(float a, float b, float u)
{
    return a + (b - a) * u;
}

bool recomp_camera_path_sample_look(const RecompCameraPath *path, float t, RecompCameraEffects *look)
{
    if (path == NULL || look == NULL || path->count < 2u) return false;
    for (unsigned i = 0u; i < path->count; ++i)
        if (!path->looked[i]) return false;
    float u;
    const unsigned span = locate(path, t, &u);
    const RecompCameraEffects *b = &path->looks[span], *c = &path->looks[span + 1u];
    *look = u < 0.5f ? *b : *c;
    look->aperture = blend(b->aperture, c->aperture, u);
    look->roll = blend(b->roll, c->roll, u);
    look->vignette = blend(b->vignette, c->vignette, u);
    look->grain = blend(b->grain, c->grain, u);
    look->exposure = blend(b->exposure, c->exposure, u);
    look->contrast = blend(b->contrast, c->contrast, u);
    look->saturation = blend(b->saturation, c->saturation, u);
    look->bloom = blend(b->bloom, c->bloom, u);
    look->light_strength = blend(b->light_strength, c->light_strength, u);
    look->light_angle = b->light_angle + remainderf(c->light_angle - b->light_angle, 2.0f * PI) * u;
    if (b->hour >= 0.0f && c->hour >= 0.0f) look->hour = blend(b->hour, c->hour, u);
    if (b->focus > 0.0f && c->focus > 0.0f) look->focus = blend(b->focus, c->focus, u);
    look->active = b->active || c->active;
    return true;
}

bool recomp_camera_path_sample(const RecompCameraPath *path, float t, RecompFreeCamera *view)
{
    if (path == NULL || view == NULL || path->count < 2u) return false;
    float u;
    const unsigned span = locate(path, t, &u);
    const unsigned ia = span > 0u ? span - 1u : 0u, ib = span, ic = span + 1u;
    const unsigned id = span + 2u < path->count ? span + 2u : path->count - 1u;
    const RecompFreeCamera *a = &path->keys[ia], *b = &path->keys[ib], *c = &path->keys[ic], *d = &path->keys[id];
    float headings[RECOMP_CAMERA_PATH_KEYS];
    unwrapped_headings(path, headings);
    *view = *b;
    for (unsigned k = 0u; k < 3u; ++k)
        view->position[k] = spline(a->position[k], b->position[k], c->position[k], d->position[k], u);
    view->heading = remainderf(spline(headings[ia], headings[ib], headings[ic], headings[id], u), 2.0f * PI);
    view->pitch = spline(a->pitch, b->pitch, c->pitch, d->pitch, u);
    view->fov = spline(a->fov, b->fov, c->fov, d->fov, u);
    view->focus = b->focus + (c->focus - b->focus) * u;
    view->speed = b->speed + (c->speed - b->speed) * u;
    return true;
}
