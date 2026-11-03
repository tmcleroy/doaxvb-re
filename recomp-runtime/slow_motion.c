#include "slow_motion.h"

#ifdef RECOMP_FULL_PROGRAM
#include "d3d_split_pose.h"
#include "game_display.h"
#include "runtime.h"

#include <math.h>
#include <string.h>

/* Smooth slow motion. In slow motion the free camera steps a frozen scene
   one game frame in every 2, 4 or 8 it shows. In the frames between, it
   shows the scene part of the way between the last two game frames, as the
   high-refresh display passes do (game_display.c), from the same places:
   the ball's position and spin, each character's place and heading and her
   three bone banks (world matrices at 0x4D3650, and the two at 0x4D5700 and
   0x4D6700), the scene lights (0x9D5240, and the copies lit from at
   0x9D50D8), the boats, and the foliage's sway. Rotations blend as rigid
   turns (d3d_split_pose.h); angles take the short way round. A character
   who moved more than half a unit in a frame, or came or went, or the ball
   more than two, is shown where she is now, not slid there.

   Joints are drawn with a second bank, each bone's inverse (0x004D72F0 + n *
   0x800), which 0x000AE3F0 derives only as the scene runs: a blended bone's
   inverse is worked out here, or her limbs come apart at the joints.

   Everything blended is game state the next step starts from, so the
   newest sample is put back exactly before the scene runs on. */
enum { FLOATS, ANGLES, WORDS, MATRIX, INVERSE, MAX_SAMPLES = 4096 };

typedef struct Sample {
    uint32_t address, kind, count;
    uint32_t before[16], after[16];
} Sample;

static struct {
    Sample samples[MAX_SAMPLES];
    unsigned count, cursor;
    bool continuous, blended;
    /* Characters the shown blend left alone; the restore leaves them too. */
    uint32_t skip;
    /* Each character's samples: [first, end). */
    unsigned actor_first[4], actor_end[4];
    uint8_t actor_type[4], actor_active[4];
} motion;

static void sample(uint32_t address, unsigned count, unsigned kind)
{
    if (motion.cursor == MAX_SAMPLES) return;
    Sample *s = motion.samples + motion.cursor;
    const bool same = motion.continuous && motion.cursor < motion.count &&
        s->address == address && s->count == count && s->kind == kind;
    memcpy(s->before, s->after, sizeof s->before);
    s->address = address;
    s->count = count;
    s->kind = kind;
    recomp_guest_load(s->after, address, count * 4u);
    if (!same) memcpy(s->before, s->after, sizeof s->before);
    ++motion.cursor;
}

static void snap(unsigned first, unsigned end)
{
    for (unsigned i = first; i < end; ++i)
        memcpy(motion.samples[i].before, motion.samples[i].after, sizeof motion.samples[i].before);
}

void recomp_slow_motion_commit(bool continuous)
{
    motion.continuous = continuous;
    motion.cursor = 0u;
    motion.blended = false;
    sample(0x00416630u, 3u, FLOATS);
    sample(0x00416690u, 3u, ANGLES);
    if (recomp_game_display_jump((const float *)motion.samples[0].before,
            (const float *)motion.samples[0].after, 2.0f)) snap(0u, motion.cursor);
    for (unsigned actor = 0u; actor < 4u; ++actor) {
        const unsigned first = motion.cursor;
        const uint8_t active = *recomp_memory(0x004d6fd0u + actor, 1u);
        const uint8_t type = *recomp_memory(0x004d6ff1u + actor * 0xbcu, 1u);
        sample(0x004d700cu + actor * 0xbcu, 3u, FLOATS);
        sample(0x004d7004u + actor * 0xbcu, 1u, ANGLES);
        for (unsigned bone = 0u; bone < 32u; ++bone) {
            sample(0x004d3650u + actor * 0x800u + bone * 64u, 16u, MATRIX);
            sample(0x004d72f0u + actor * 0x800u + bone * 64u, 16u, INVERSE);
            sample(0x004d5700u + actor * 0x400u + bone * 32u, 8u, FLOATS);
            sample(0x004d6700u + actor * 0x200u + bone * 16u, 4u, FLOATS);
        }
        if (!continuous || active != motion.actor_active[actor] || type != motion.actor_type[actor] ||
            recomp_game_display_jump((const float *)motion.samples[first].before,
                (const float *)motion.samples[first].after, 0.5f)) snap(first, motion.cursor);
        motion.actor_first[actor] = first;
        motion.actor_end[actor] = motion.cursor;
        motion.actor_active[actor] = active;
        motion.actor_type[actor] = type;
    }
    sample(0x00327cd0u, 4u, FLOATS);
    sample(0x00327d10u, 5u, ANGLES);
    sample(0x005bb980u, 4u, FLOATS);
    sample(0x005bb97cu, 1u, ANGLES);
    for (unsigned boat = 0u; boat < 3u; ++boat) {
        sample(0x00327520u + boat * 0x150u, 4u, FLOATS);
        sample(0x00327540u + boat * 0x150u, 3u, ANGLES);
    }
    for (unsigned record = 0u; record < 4u; ++record) {
        const uint32_t lights = 0x009d5240u + record * 0x58u, work = 0x009d50d8u + record * 0x58u;
        sample(lights + 4u, 8u, FLOATS);
        sample(lights + 36u, 2u, ANGLES);
        sample(lights + 44u, 3u, FLOATS);
        sample(work + 4u, 8u, FLOATS);
        sample(work + 36u, 2u, ANGLES);
        sample(work + 44u, 3u, FLOATS);
    }
    for (unsigned group = 0u; group < 2u; ++group) {
        uint32_t descriptor = *recomp_memory_u32(0x00326fa0u + group * 4u);
        if (descriptor != 0u) {
            const unsigned count = *recomp_memory_u16(descriptor + 16u);
            const uint32_t records = *recomp_memory_u32(descriptor);
            for (unsigned i = 0u; records != 0u && i < count; ++i) {
                sample(records + i * 64u + 44u, 1u, WORDS);
                sample(records + i * 64u + 56u, 1u, WORDS);
            }
        }
        descriptor = *recomp_memory_u32(0x00327408u + group * 4u);
        if (descriptor != 0u) {
            const int count = (int16_t)*recomp_memory_u16(descriptor + 4u);
            const uint32_t records = *recomp_memory_u32(descriptor);
            for (int i = 0; records != 0u && i < count; ++i)
                sample(records + (uint32_t)i * 0x2cu + 0x24u, 2u, WORDS);
        }
    }
    motion.count = motion.cursor;
}

/* The inverse of a bone's world matrix (rows: three axes, then the place). */
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

/* The bone matrix last written, which the inverse after it follows. */
static float written_bone[16];

static void blend(const Sample *s, float alpha, uint32_t out[16])
{
    if (s->kind == INVERSE) {
        if (alpha >= 1.0f || !invert(written_bone, (float *)out)) memcpy(out, s->after, s->count * 4u);
        return;
    }
    if (alpha >= 1.0f || memcmp(s->before, s->after, s->count * 4u) == 0) {
        memcpy(out, s->after, s->count * 4u);
        return;
    }
    if (s->kind == MATRIX) {
        if (!recomp_split_rigid_matrix((const float *)s->before, (const float *)s->after, alpha, (float *)out))
            memcpy(out, s->after, s->count * 4u);
        return;
    }
    for (unsigned i = 0u; i < s->count; ++i) {
        if (s->kind == ANGLES) {
            out[i] = recomp_game_display_angle(s->before[i], s->after[i], alpha);
        } else if (s->kind == WORDS) {
            out[i] = s->before[i] + (uint32_t)(int32_t)((int32_t)(s->after[i] - s->before[i]) * (double)alpha);
        } else {
            float a, b;
            memcpy(&a, s->before + i, 4u);
            memcpy(&b, s->after + i, 4u);
            const float value = recomp_game_display_lerp(a, b, alpha);
            memcpy(out + i, &value, 4u);
        }
    }
}

static void write(float alpha, uint32_t skip)
{
    if (motion.count == 0u) return;
    alpha = alpha < 0.0f ? 0.0f : alpha;
    for (unsigned n = 0u; n < motion.count; ++n) {
        bool skipped = false;
        for (unsigned actor = 0u; actor < 4u; ++actor)
            if ((skip & (1u << actor)) != 0u && n >= motion.actor_first[actor] && n < motion.actor_end[actor])
                skipped = true;
        if (skipped) continue;
        const Sample *s = motion.samples + n;
        uint32_t out[16];
        blend(s, alpha, out);
        if (s->kind == MATRIX) memcpy(written_bone, out, sizeof written_bone);
        recomp_guest_store(s->address, out, s->count * 4u);
    }
}

void recomp_slow_motion_apply(float alpha, uint32_t skip)
{
    write(alpha, skip);
    motion.skip = skip;
    motion.blended = motion.count != 0u;
}

void recomp_slow_motion_restore(void)
{
    if (!motion.blended) return;
    write(1.0f, motion.skip);
    motion.blended = false;
}

bool recomp_slow_motion_blended(void)
{
    return motion.blended;
}

void recomp_slow_motion_reset(void)
{
    motion.count = 0u;
    motion.blended = false;
}
#else
void recomp_slow_motion_commit(bool continuous)
{
    (void)continuous;
}

void recomp_slow_motion_apply(float alpha, uint32_t skip)
{
    (void)alpha;
    (void)skip;
}

void recomp_slow_motion_restore(void)
{
}

bool recomp_slow_motion_blended(void)
{
    return false;
}

void recomp_slow_motion_reset(void)
{
}
#endif
