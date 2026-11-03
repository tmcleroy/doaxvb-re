#include "pose_library.h"

#include <math.h>
#include <string.h>

/* a * b for row-vector matrices (a point is p * a * b). */
static void multiply(const float a[16], const float b[16], float out[16])
{
    for (unsigned r = 0u; r < 4u; ++r)
        for (unsigned c = 0u; c < 4u; ++c) {
            float sum = 0.0f;
            for (unsigned k = 0u; k < 4u; ++k) sum += a[r * 4u + k] * b[k * 4u + c];
            out[r * 4u + c] = sum;
        }
}

/* The inverse of an affine matrix (three axes, then the place). */
static bool invert(const float m[16], float out[16])
{
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const float A = e * i - f * h, B = f * g - d * i, C = d * h - e * g;
    const float det = a * A + b * B + c * C;
    if (!(fabsf(det) > 1e-12f)) return false;
    const float k = 1.0f / det;
    const float r[9] = {A * k, (c * h - b * i) * k, (b * f - c * e) * k,
                        B * k, (a * i - c * g) * k, (c * d - a * f) * k,
                        C * k, (b * g - a * h) * k, (a * e - b * d) * k};
    for (unsigned row = 0u; row < 3u; ++row) {
        for (unsigned col = 0u; col < 3u; ++col) out[row * 4u + col] = r[row * 3u + col];
        out[row * 4u + 3u] = 0.0f;
    }
    for (unsigned col = 0u; col < 3u; ++col)
        out[12u + col] = -(m[12] * r[col] + m[13] * r[3u + col] + m[14] * r[6u + col]);
    out[15] = 1.0f;
    return true;
}

/* Where she stands and which way she faces: level axes turned to the hips'
   facing (their third axis on the ground), at the hips' place on the ground.
   A pose kept in this frame keeps its own height and tilt. */
static bool standing(const float hips[16], float frame[16])
{
    float fx = hips[8], fz = hips[10];
    const float length = sqrtf(fx * fx + fz * fz);
    if (!(length > 1e-6f)) return false;
    fx /= length;
    fz /= length;
    const float m[16] = {fz, 0.0f, -fx, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, fx, 0.0f, fz, 0.0f, hips[12], 0.0f, hips[14], 1.0f};
    memcpy(frame, m, sizeof m);
    return true;
}

bool recomp_pose_keep(const float bones[RECOMP_POSE_BONES][16], uint8_t type, RecompPoseSlot *slot)
{
    float frame[16], frame_inverse[16];
    if (slot == NULL || bones[0][15] == 0.0f || !standing(bones[0], frame) || !invert(frame, frame_inverse))
        return false;
    for (unsigned bone = 0u; bone < RECOMP_POSE_BONES; ++bone) {
        if (bones[bone][15] == 0.0f) memset(slot->bones[bone], 0, sizeof slot->bones[bone]);
        else multiply(bones[bone], frame_inverse, slot->bones[bone]);
    }
    slot->type = type;
    slot->used = true;
    return true;
}

void recomp_pose_put_back(const RecompPoseSlot *slot, const float hips[16], float bones[RECOMP_POSE_BONES][16])
{
    float frame[16];
    if (slot == NULL || !slot->used || !standing(hips, frame)) return;
    for (unsigned bone = 0u; bone < RECOMP_POSE_BONES; ++bone) {
        if (slot->bones[bone][15] == 0.0f || bones[bone][15] == 0.0f) continue;
        multiply(slot->bones[bone], frame, bones[bone]);
    }
}
