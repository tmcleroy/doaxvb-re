#include "pose_library.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(bool ok, const char *what)
{
    if (!ok) {
        ++failures;
        fprintf(stderr, "pose library test failed: %s\n", what);
    }
}

static void turned(float angle, float x, float y, float z, float out[16])
{
    const float c = cosf(angle), s = sinf(angle);
    const float m[16] = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, x, y, z, 1};
    memcpy(out, m, sizeof m);
}

int main(void)
{
    float bones[RECOMP_POSE_BONES][16];
    memset(bones, 0, sizeof bones);
    turned(0.3f, 1.0f, 0.9f, 2.0f, bones[0]);
    turned(0.8f, 1.2f, 1.4f, 2.1f, bones[1]);
    turned(-0.4f, 0.8f, 0.5f, 1.9f, bones[5]);
    RecompPoseSlot slot;
    check(recomp_pose_keep(bones, 5u, &slot) && slot.used && slot.type == 5u, "keeps a pose");
    /* Put back where she stood: the same bones. */
    float again[RECOMP_POSE_BONES][16];
    memcpy(again, bones, sizeof again);
    memset(again[1], 0, sizeof again[1]);
    again[1][15] = 1.0f;
    recomp_pose_put_back(&slot, bones[0], again);
    float worst = 0.0f;
    for (unsigned b = 0; b < 2u; ++b)
        for (unsigned i = 0; i < 16u; ++i) worst = fmaxf(worst, fabsf(again[b][i] - bones[b][i]));
    check(worst < 1e-5f, "where she stood, the same bones");

    /* Elsewhere, facing another way and tilted, mid-jump: the pose keeps its
       height and shape, turned to her facing. */
    float hips[16];
    turned(1.3f, -4.0f, 1.6f, 7.0f, hips);
    memcpy(again[0], hips, sizeof hips);
    recomp_pose_put_back(&slot, hips, again);
    check(fabsf(again[0][13] - bones[0][13]) < 1e-4f && fabsf(again[1][13] - bones[1][13]) < 1e-4f,
        "the pose keeps its height");
    check(fabsf(again[0][12] + 4.0f) < 1e-4f && fabsf(again[0][14] - 7.0f) < 1e-4f, "the hips go where she is");
    const float dx0 = bones[1][12] - bones[0][12], dz0 = bones[1][14] - bones[0][14];
    const float dx1 = again[1][12] - again[0][12], dz1 = again[1][14] - again[0][14];
    check(fabsf(sqrtf(dx0 * dx0 + dz0 * dz0) - sqrtf(dx1 * dx1 + dz1 * dz1)) < 1e-4f, "the pose keeps its shape");
    check(again[2][15] == 0.0f, "an unused bone stays unused");

    if (failures == 0) printf("pose library test passed\n");
    return failures == 0 ? 0 : 1;
}
