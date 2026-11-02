#include "camera_path.h"

#include <math.h>
#include <string.h>

static const float PI = 3.14159265f;

bool recomp_camera_path_add(RecompCameraPath *path, const RecompFreeCamera *view)
{
    if (path == NULL || view == NULL || path->count >= RECOMP_CAMERA_PATH_KEYS) return false;
    path->keys[path->count++] = *view;
    return true;
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

bool recomp_camera_path_sample(const RecompCameraPath *path, float t, RecompFreeCamera *view)
{
    if (path == NULL || view == NULL || path->count < 2u) return false;
    if (!(t > 0.0f)) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    /* Ease in and out over the whole path. */
    t = t * t * (3.0f - 2.0f * t);
    const unsigned spans = path->count - 1u;
    float at = t * (float)spans;
    unsigned span = (unsigned)at;
    if (span >= spans) span = spans - 1u;
    const float u = at - (float)span;
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
