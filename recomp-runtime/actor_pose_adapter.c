#include "actor_pose_adapter.h"
#include "actor_pose.h"
#include "scene_freeze_adapter.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef RECOMP_FULL_PROGRAM
#include "runtime.h"
#include <stdlib.h>
#include "animation_probe.h"
#include "d3d_frame_adapter.h"

/* Animation select: plays a chosen clip on a character and holds it.

   Up to four characters animate at once. 0x4D6FD0 + n is nonzero while
   character n is active. Each has two animation layers of 0x80 bytes, the
   first at 0x5E0800 + n * 0x100. Each frame 0x000AC100 updates a character's
   layers; when the first layer's byte +0x74 is set, it clears it and calls
   0x000AEC80, which starts the clip numbered at +0x62 (+0x60 carries the same
   number). Bit 0x2000 of the number picks the shared bank; otherwise the
   layer's byte +0x6E picks the character's bank. The bank table at 0x5DD844
   has 12-byte entries whose first word is the bank, the shared bank fifth.
   A bank is 1000 clip pointers; a clip the scene did not load points at the
   placeholder 0xFA4 bytes into the bank.

   A clip carries its own root position, so a new clip would move the
   character. The character's root block at 0x4D6FF0 + n * 0xBC holds its
   position at +8; it is put back each frame while a clip is held, after the
   game's animation update and before the frame is drawn. While held, a clip
   the game's own logic starts in its place is replaced again.

   The root block does not hold everything: many clips also move the body
   away from the root. 0x000B01E0 builds a character's 32 bone matrices from
   the sampled clip, row-vector world matrices of 64 bytes at
   0x4D3650 + n * 0x800 with the translation in floats 12 to 14. After it
   builds a held character, every bone is moved across the ground so the first
   bone, the hips, stays where it stood when the hold began. Height is left
   alone, so sitting and standing clips keep their own.

   A character can also be hidden: after her skeleton is built every bone is
   moved HIDE_DEPTH below where it stood, out of any view above the ground.
   A frozen scene builds no skeletons, so hiding and showing also move the
   bones already built; each build leaves a hidden character's bones that
   far down, so the two agree.

   In a match, character n is player n; character select's choice for the
   player, at 0x9DAA68 + n * 0xB8, holds her character number at +1, and
   the English names are pointed to from 0x3245BC by that number. */
enum {
    ACTOR_COUNT = 4u,
    ACTOR_ACTIVE = 0x004D6FD0u,
    LAYER_FIRST = 0x005E0800u,
    LAYER_STRIDE = 0x100u,
    LAYER_CLIP_ALIAS = 0x60u,
    LAYER_CLIP = 0x62u,
    LAYER_BANK = 0x6Eu,
    LAYER_START = 0x74u,
    BANK_TABLE = 0x005DD844u,
    BANK_ENTRY_SIZE = 12u,
    SHARED_BANK_ENTRY = 4u,
    BANK_PLACEHOLDER = 0xFA4u,
    ROOT_FIRST = 0x004D6FF0u,
    /* Each character's look record: +0x164 her look weight (float). */
    LOOK_FIRST = 0x005E5FE8u,
    LOOK_STRIDE = 0x180u,
    ROOT_STRIDE = 0xBCu,
    ROOT_POSITION = 0x08u,
    BUILD_SKELETON = 0x000B01E0u,
    BUILD_PALETTE = 0x000636D0u,
    /* The draw palette 0x000636D0 builds: up to four world matrices. */
    PALETTE = 0x00B25840u,
    PALETTES_KEPT = 32u,
    BONE_FIRST = 0x004D3650u,
    BONE_STRIDE = 0x800u,
    BONE_COUNT = 32u,
    BONE_SIZE = 64u,
    SCENE = 0x005DECE0u,
    SCENE_MATCH = 0,
    CHOICE = 0x009DAA68u,
    CHOICE_SIZE = 0xB8u,
    CHOICE_CHARACTER = 1u,
    NAMES = 0x003245BCu,
    NAME_COUNT = 9u,
    NO_CLIP = 0xFFFFu,
};

static const float HIDE_DEPTH = 100.0f;

void sub_000B01E0(void);
void sub_000AE350(void);
void sub_000AE3F0(void);

/* Rebuilding a frozen scene's skeletons for eye contact. */
static bool rebuilding_frozen;

/* Where the free camera has put each character: moved along the ground and
   turned about her hips, applied to her bones after each build. `pivot` is
   her hips before the move, as last built; `placement_dirty` asks a frozen
   scene to build her again. */
static struct {
    bool set;
    float offset[2];  /* x, z */
    float turn;       /* radians about the vertical */
    float pivot[3];
} placed[4];
static bool placement_dirty;

/* Turns (x, z) about the vertical by `angle`, as the key light turns. */
static void turn_level(float *x, float *z, float angle)
{
    const float c = cosf(angle), s = sinf(angle), a = *x, b = *z;
    *x = c * a + s * b;
    *z = -s * a + c * b;
}

/* Moves a just-built skeleton to where she was put. */
static void place_bones(uint32_t actor);

/* The pose library, and the slot each character holds (-1 none). */
static RecompPoseSlot library[RECOMP_POSE_SLOTS];
static int held_pose[4] = {-1, -1, -1, -1};
static void hold_bones(uint32_t actor);
void sub_000636D0(void);

static struct {
    uint32_t actor;
    bool holding;
    uint16_t clip;
    float position[3];
    /* The hips' place on the ground when the hold began. */
    float hips[2];
    bool hips_known;
    uint32_t hidden;
    /* The held clip's place in its list, from 1, and the list's length. */
    uint32_t listed, listed_count;
    /* A clip being looked for, not yet played. */
    bool browsing;
    /* Released: the next look starts at the list's first clip. */
    bool from_start;
    uint16_t browse_clip;
    uint32_t browse_listed, browse_count;
} pose;

static bool active(uint32_t actor)
{
    return actor < ACTOR_COUNT && *recomp_memory_i8(ACTOR_ACTIVE + actor) != 0;
}

static uint32_t layer_of(uint32_t actor)
{
    return LAYER_FIRST + actor * LAYER_STRIDE;
}

static uint32_t root_of(uint32_t actor)
{
    return ROOT_FIRST + actor * ROOT_STRIDE;
}

static uint32_t bone_of(uint32_t actor, uint32_t bone)
{
    return BONE_FIRST + actor * BONE_STRIDE + bone * BONE_SIZE;
}

static void lower_bones(uint32_t actor, float depth);

void recomp_actor_pose_status(RecompActorPoseStatus *status)
{
    status->actor = pose.actor;
    status->active = active(pose.actor);
    status->holding = pose.holding;
    status->clip = pose.clip;
    status->position = pose.holding ? pose.listed : 0u;
    status->count = pose.holding ? pose.listed_count : 0u;
    status->browsing = pose.browsing;
    status->browse_clip = pose.browse_clip;
    status->browse_position = pose.browse_listed;
    status->browse_count = pose.browse_count;
}

void recomp_actor_pose_name(uint32_t actor, char *name, uint32_t size)
{
    if (name == NULL || size == 0u) return;
    snprintf(name, size, "Character %u", (unsigned)actor + 1u);
    if (actor >= ACTOR_COUNT || *recomp_memory(SCENE, 1u) != SCENE_MATCH) return;
    uint8_t character = *recomp_memory(CHOICE + actor * CHOICE_SIZE + CHOICE_CHARACTER, 1u);
    if (character >= NAME_COUNT) return;
    uint32_t text = *recomp_memory_u32(NAMES + character * 4u);
    if (text == 0u) return;
    char found[32];
    recomp_guest_load(found, text, sizeof found - 1u);
    found[sizeof found - 1u] = '\0';
    for (uint32_t i = 0u; found[i] != '\0'; ++i) {
        if ((unsigned char)found[i] < 0x20u || (unsigned char)found[i] > 0x7Eu) return;
    }
    if (found[0] != '\0') snprintf(name, size, "%s", found);
}

void recomp_actor_pose_choose(uint32_t actor)
{
    if (actor >= ACTOR_COUNT) return;
    if (actor != pose.actor) recomp_actor_pose_release();
    pose.actor = actor;
}

void recomp_actor_pose_set_hidden(uint32_t mask)
{
    for (uint32_t actor = 0u; actor < ACTOR_COUNT; ++actor) {
        uint32_t bit = 1u << actor;
        if ((mask & bit) != 0u && (pose.hidden & bit) == 0u) lower_bones(actor, HIDE_DEPTH);
        if ((mask & bit) == 0u && (pose.hidden & bit) != 0u) lower_bones(actor, -HIDE_DEPTH);
    }
    pose.hidden = mask;
}

bool recomp_actor_pose_hips(uint32_t actor, float point[3])
{
    if (!active(actor) || (pose.hidden & (1u << actor)) != 0u) return false;
    recomp_guest_load(point, bone_of(actor, 0u) + 12u * sizeof(float), 3u * sizeof(float));
    return true;
}

bool recomp_actor_pose_focus(float point[3])
{
    if (!active(pose.actor)) recomp_actor_pose_next_actor();
    if (!active(pose.actor)) return false;
    recomp_guest_load(point, bone_of(pose.actor, 0u) + 12u * sizeof(float), 3u * sizeof(float));
    return true;
}

bool recomp_actor_pose_facing(float direction[3])
{
    if (!active(pose.actor)) return false;
    float matrix[16];
    recomp_guest_load(matrix, bone_of(pose.actor, 0u), sizeof matrix);
    /* The hips' third axis points the way she faces. */
    direction[0] = matrix[8];
    direction[1] = 0.0f;
    direction[2] = matrix[10];
    float length = sqrtf(direction[0] * direction[0] + direction[2] * direction[2]);
    if (!(length > 1e-4f)) return false;
    direction[0] /= length;
    direction[2] /= length;
    return true;
}

static void lower_bones(uint32_t actor, float depth)
{
    for (uint32_t bone = 0u; bone < BONE_COUNT; ++bone) {
        float matrix[16];
        uint32_t address = bone_of(actor, bone);
        recomp_guest_load(matrix, address, sizeof matrix);
        if (matrix[15] == 0.0f) continue;
        matrix[13] -= depth;
        recomp_guest_store(address, matrix, sizeof matrix);
    }
}

static void build_skeleton(void);

static void build_skeleton_inner(void)
{
    uint32_t actor = recomp_runtime.registers.eax;
    /* Some scenes build skeletons in a task that also draws, which a frozen
       scene keeps running (the pool): keep the pose already built, unless
       eye contact rebuilds it (recomp_actor_pose_look_while_frozen). */
    if (actor < ACTOR_COUNT && recomp_scene_freeze_holding() && !rebuilding_frozen) {
        recomp_runtime.registers.esp += 4u;
        return;
    }
    RecompFunction observed = recomp_animation_probe_lookup_manual(BUILD_SKELETON);
    if (observed != NULL) observed();
    else sub_000B01E0();
    if (actor < ACTOR_COUNT && (pose.hidden & (1u << actor)) != 0u) {
        lower_bones(actor, HIDE_DEPTH);
        return;
    }
    if (!pose.holding || actor != pose.actor || !pose.hips_known) return;

    float hips[3];
    recomp_guest_load(hips, bone_of(actor, 0u) + 12u * sizeof(float), sizeof hips);
    float dx = pose.hips[0] - hips[0], dz = pose.hips[1] - hips[2];
    for (uint32_t bone = 0u; bone < BONE_COUNT; ++bone) {
        float matrix[16];
        uint32_t address = bone_of(actor, bone);
        recomp_guest_load(matrix, address, sizeof matrix);
        if (matrix[15] == 0.0f) continue;
        matrix[12] += dx;
        matrix[14] += dz;
        recomp_guest_store(address, matrix, sizeof matrix);
    }
}

/* Each draw of a character is preceded by 0x000636D0 building its palette
   (0x00B25840) from her bones (the bank in [ESP+4]), which 0x0017D670 then
   loads as WORLD0..WORLD3. A frame's palettes from character banks are kept,
   so a draw can be told for hers by its WORLD matrix; scenery and props are
   drawn from palettes of their own, or none. */
static struct {
    uint32_t frame;
    unsigned count;
    float matrices[PALETTES_KEPT][4][16];
} palettes;

static void build_palette(void)
{
    const uint32_t bank = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    RecompFunction observed = recomp_animation_probe_lookup_manual(BUILD_PALETTE);
    if (observed != NULL) observed();
    else sub_000636D0();
    const uint32_t offset = bank - BONE_FIRST;
    if (bank < BONE_FIRST || offset % BONE_STRIDE != 0u || offset / BONE_STRIDE >= ACTOR_COUNT) return;
    const uint32_t frame = recomp_d3d_frame_adapter_swap_counter();
    if (palettes.frame != frame) {
        palettes.frame = frame;
        palettes.count = 0u;
    }
    if (palettes.count == PALETTES_KEPT) return;
    recomp_guest_load(palettes.matrices[palettes.count++], PALETTE, sizeof palettes.matrices[0]);
}

bool recomp_actor_pose_character_world(const float world[16])
{
    if (palettes.frame == recomp_d3d_frame_adapter_swap_counter())
        for (unsigned p = 0u; p < palettes.count; ++p)
            for (unsigned m = 0u; m < 4u; ++m)
                if (memcmp(palettes.matrices[p][m], world, sizeof palettes.matrices[p][m]) == 0) return true;
    /* Rigid pieces are drawn at a bone itself. */
    for (uint32_t actor = 0u; actor < ACTOR_COUNT; ++actor) {
        if (!active(actor)) continue;
        const uint8_t *bones = recomp_memory(bone_of(actor, 0u), BONE_COUNT * BONE_SIZE);
        for (unsigned bone = 0u; bone < BONE_COUNT; ++bone)
            if (memcmp(bones + bone * BONE_SIZE, world, 64u) == 0) return true;
    }
    return false;
}

RecompFunction recomp_actor_pose_lookup_manual(uint32_t guest_address)
{
    if (guest_address == BUILD_PALETTE) return build_palette;
    return guest_address == BUILD_SKELETON ? build_skeleton : NULL;
}

static bool load_bank(uint32_t entry, uint32_t *bank, uint32_t entries[RECOMP_ACTOR_POSE_BANK_SIZE])
{
    *bank = *recomp_memory_u32(BANK_TABLE + entry * BANK_ENTRY_SIZE);
    if (*bank == 0u) return false;
    recomp_guest_load(entries, *bank, RECOMP_ACTOR_POSE_BANK_SIZE * sizeof *entries);
    return true;
}

static void request(uint32_t actor, uint16_t clip)
{
    uint32_t layer = layer_of(actor);
    *recomp_memory_u16(layer + LAYER_CLIP_ALIAS) = clip;
    *recomp_memory_u16(layer + LAYER_CLIP) = clip;
    *recomp_memory_i8(layer + LAYER_START) = 1;
}

void recomp_actor_pose_next_actor(void)
{
    for (uint32_t step = 1u; step <= ACTOR_COUNT; ++step) {
        uint32_t actor = (pose.actor + step) % ACTOR_COUNT;
        if (active(actor)) {
            if (actor != pose.actor) recomp_actor_pose_release();
            pose.actor = actor;
            fprintf(stderr, "recomp pose: character %u\n", actor);
            return;
        }
    }
}

static RecompActorPoseClips clips;

/* Lists the clips the scene loaded for the chosen character and the shared
   ones. False when no character is active. */
static bool list_clips(void)
{
    static uint32_t character[RECOMP_ACTOR_POSE_BANK_SIZE], shared[RECOMP_ACTOR_POSE_BANK_SIZE];
    uint32_t character_bank = 0u, shared_bank = 0u;

    if (!active(pose.actor)) recomp_actor_pose_next_actor();
    if (!active(pose.actor)) return false;
    uint32_t layer = layer_of(pose.actor);
    bool has_character = load_bank(*recomp_memory_i8(layer + LAYER_BANK) & 0xFFu, &character_bank, character);
    bool has_shared = load_bank(SHARED_BANK_ENTRY, &shared_bank, shared);
    recomp_actor_pose_list(&clips, has_character ? character : NULL, character_bank + BANK_PLACEHOLDER,
        has_shared ? shared : NULL, shared_bank + BANK_PLACEHOLDER);
    return true;
}

bool recomp_actor_pose_browse(int step)
{
    if (!list_clips()) return false;
    /* After a release the list starts again from its first clip. */
    uint16_t current = pose.browsing ? pose.browse_clip :
        pose.holding ? pose.clip :
        pose.from_start ? NO_CLIP : *recomp_memory_u16(layer_of(pose.actor) + LAYER_CLIP);
    uint16_t next;
    uint32_t position;
    if (!recomp_actor_pose_next(&clips, current, step, &next, &position)) return false;
    pose.browsing = true;
    pose.from_start = false;
    pose.browse_clip = next;
    pose.browse_listed = position + 1u;
    pose.browse_count = clips.count;
    return true;
}

bool recomp_actor_pose_play_browsed(void)
{
    if (!pose.browsing) return false;
    pose.browsing = false;
    if (!active(pose.actor)) return false;
    if (!pose.holding) {
        float hips[3];
        recomp_guest_load(pose.position, root_of(pose.actor) + ROOT_POSITION, sizeof pose.position);
        recomp_guest_load(hips, bone_of(pose.actor, 0u) + 12u * sizeof(float), sizeof hips);
        pose.hips[0] = hips[0];
        pose.hips[1] = hips[2];
        pose.hips_known = true;
        pose.holding = true;
    }
    pose.clip = pose.browse_clip;
    pose.listed = pose.browse_listed;
    pose.listed_count = pose.browse_count;
    request(pose.actor, pose.clip);
    fprintf(stderr, "recomp pose: character %u clip %04x (%u of %u)\n",
        pose.actor, pose.clip, pose.listed, pose.listed_count);
    return true;
}

bool recomp_actor_pose_step_clip(int step)
{
    return recomp_actor_pose_browse(step) && recomp_actor_pose_play_browsed();
}

void recomp_actor_pose_release(void)
{
    pose.browsing = false;
    pose.from_start = true;
    if (!pose.holding) return;
    pose.holding = false;
    fprintf(stderr, "recomp pose: character %u released\n", pose.actor);
}

/* Pose history: every character's bones after each frame's skeleton build,
   for the last HISTORY frames, so a frozen scene can step back through them.
   A frozen scene builds no skeletons, so bones put back stay until it runs. */
enum { HISTORY = 600u };
static struct {
    uint32_t frame[HISTORY];
    uint8_t bones[HISTORY][ACTOR_COUNT][BONE_COUNT * BONE_SIZE];
    /* Each character's root block: bones are drawn relative to it. */
    uint8_t roots[HISTORY][ACTOR_COUNT][ROOT_STRIDE];
    unsigned newest, count, back;
} history;

static void record_bones(uint32_t actor)
{
    const uint32_t frame = recomp_d3d_frame_adapter_swap_counter();
    if (history.count == 0u || history.frame[history.newest] != frame) {
        history.newest = (history.newest + 1u) % HISTORY;
        history.frame[history.newest] = frame;
        if (history.count < HISTORY) ++history.count;
        /* A new frame starts from the last one's bones for characters not built in it. */
        const unsigned last = (history.newest + HISTORY - 1u) % HISTORY;
        if (history.count > 1u) {
            memcpy(history.bones[history.newest], history.bones[last], sizeof history.bones[0]);
            memcpy(history.roots[history.newest], history.roots[last], sizeof history.roots[0]);
        }
    }
    history.back = 0u;
    recomp_guest_load(history.bones[history.newest][actor], bone_of(actor, 0u), BONE_COUNT * BONE_SIZE);
    recomp_guest_load(history.roots[history.newest][actor], root_of(actor), ROOT_STRIDE);
}

/* Eye contact (recomp_actor_pose_set_look): the game's own look-at turns a
   character's neck and head toward the ball (0x00416630) by her look weight
   (+0x164 of her look record, 0x005E5FE8 + n * 0x180, which 0x000B01E0
   works out from EAX and hands to the look-at, 0x000B1B50, in ESI). For
   the build, the camera stands in for the ball at full weight; then each
   eye bone (21 and 17) turns toward the camera about its own centre, its +Z
   the line of sight. */
static struct {
    bool on, eyes;
    float eye[3];
} look;

void recomp_actor_pose_set_look(bool on, bool eyes, const float eye[3])
{
    look.on = on;
    look.eyes = eyes;
    if (eye != NULL) memcpy(look.eye, eye, sizeof look.eye);
}

static void aim_eye(uint32_t actor, uint32_t bone)
{
    float m[16];
    recomp_guest_load(m, bone_of(actor, bone), sizeof m);
    if (m[15] == 0.0f) return;
    float d[3] = {look.eye[0] - m[12], look.eye[1] - m[13], look.eye[2] - m[14]}, local[3];
    for (unsigned r = 0u; r < 3u; ++r) local[r] = d[0] * m[r * 4u] + d[1] * m[r * 4u + 1u] + d[2] * m[r * 4u + 2u];
    const float limit = 0.4f;
    float yaw = atan2f(local[0], local[2]), pitch = atan2f(-local[1], sqrtf(local[0] * local[0] + local[2] * local[2]));
    if (!(local[2] > 0.0f)) return; /* behind her: leave the eyes */
    yaw = yaw > limit ? limit : yaw < -limit ? -limit : yaw;
    pitch = pitch > limit ? limit : pitch < -limit ? -limit : pitch;
    /* In the bone's own frame: turn about its Y, then its X; rows are axes. */
    const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    const float turn[3][3] = {{cy, 0.0f, -sy}, {sy * sp, cp, cy * sp}, {sy * cp, -sp, cy * cp}};
    float out[16];
    memcpy(out, m, sizeof out);
    for (unsigned r = 0u; r < 3u; ++r)
        for (unsigned c = 0u; c < 3u; ++c)
            out[r * 4u + c] = turn[r][0] * m[c] + turn[r][1] * m[4u + c] + turn[r][2] * m[8u + c];
    recomp_guest_store(bone_of(actor, bone), out, sizeof out);
}

static void build_skeleton(void)
{
    const uint32_t actor = recomp_runtime.registers.eax;
    const uint32_t state_address = LOOK_FIRST + actor * LOOK_STRIDE;
    /* A frozen scene's build is skipped (build_skeleton_inner), unless the
       free camera builds it again (recomp_actor_pose_look_while_frozen). */
    const bool skipped = actor < ACTOR_COUNT && recomp_scene_freeze_holding() && !rebuilding_frozen;
    const bool looking = look.on && actor < ACTOR_COUNT && !skipped;
    float ball[3], weight = 0.0f;
    if (looking) {
        recomp_guest_load(ball, 0x00416630u, sizeof ball);
        recomp_guest_load(&weight, state_address + 0x164u, sizeof weight);
        const float full = 1.0f;
        /* A girl who was moved looks from where the game thinks she is. */
        float eye[3] = {look.eye[0], look.eye[1], look.eye[2]};
        if (placed[actor].set) {
            float x = eye[0] - placed[actor].pivot[0] - placed[actor].offset[0];
            float z = eye[2] - placed[actor].pivot[2] - placed[actor].offset[1];
            turn_level(&x, &z, -placed[actor].turn);
            eye[0] = placed[actor].pivot[0] + x;
            eye[2] = placed[actor].pivot[2] + z;
        }
        recomp_guest_store(0x00416630u, eye, sizeof eye);
        recomp_guest_store(state_address + 0x164u, &full, sizeof full);
    }
    build_skeleton_inner();
    if (actor < ACTOR_COUNT && !skipped) hold_bones(actor);
    if (actor < ACTOR_COUNT && !skipped) place_bones(actor);
    if (looking) {
        recomp_guest_store(0x00416630u, ball, sizeof ball);
        recomp_guest_store(state_address + 0x164u, &weight, sizeof weight);
        if (look.eyes && (pose.hidden & (1u << actor)) == 0u) {
            aim_eye(actor, 21u);
            aim_eye(actor, 17u);
        }
    }
    if (actor < ACTOR_COUNT && !skipped && !rebuilding_frozen) record_bones(actor);
}

/* A frozen scene builds no skeletons, so eye contact would leave her looking
   where she was. With it on, each frozen frame builds every character's
   skeleton again as the game does when it pauses its own world (0x000AF380
   with 0x004D56EF set: the build, then the joint banks from the bones,
   0x000AE350 and 0x000AE3F0, without stepping the animation), so her head
   follows the camera. Not added to the pose history. 0x000AE3F0 inverts
   each bone through the scratch matrix at 0x00A24190, which is put back. */
void recomp_actor_pose_look_while_frozen(void)
{
    if ((!look.on && !placement_dirty) || !recomp_scene_freeze_holding() || history.back != 0u) return;
    placement_dirty = false;
    const RecompRegisters registers = recomp_runtime.registers;
    RecompFpuContext fpu;
    recomp_fpu_context_save(&fpu);
    uint8_t scratch[64];
    recomp_guest_load(scratch, 0x00A24190u, sizeof scratch);
    rebuilding_frozen = true;
    for (uint32_t actor = 0u; actor < ACTOR_COUNT; ++actor) {
        if (!active(actor)) continue;
        recomp_runtime.registers.esp = registers.esp - 4u;
        *recomp_memory_u32(recomp_runtime.registers.esp) = 0u;
        recomp_runtime.registers.eax = actor;
        build_skeleton();
        recomp_runtime.registers.esp = registers.esp - 4u;
        *recomp_memory_u32(recomp_runtime.registers.esp) = 0u;
        recomp_runtime.registers.edx = actor;
        sub_000AE350();
        recomp_runtime.registers.esp = registers.esp - 4u;
        *recomp_memory_u32(recomp_runtime.registers.esp) = 0u;
        recomp_runtime.registers.eax = actor;
        sub_000AE3F0();
    }
    rebuilding_frozen = false;
    recomp_guest_store(0x00A24190u, scratch, sizeof scratch);
    recomp_runtime.registers = registers;
    recomp_fpu_context_restore(&fpu);
}

static void place_bones(uint32_t actor)
{
    if (!placed[actor].set) return;
    recomp_guest_load(placed[actor].pivot, bone_of(actor, 0u) + 12u * sizeof(float), sizeof placed[actor].pivot);
    const float *p = placed[actor].pivot, turn = placed[actor].turn;
    for (uint32_t bone = 0u; bone < BONE_COUNT; ++bone) {
        float m[16];
        const uint32_t address = bone_of(actor, bone);
        recomp_guest_load(m, address, sizeof m);
        if (m[15] == 0.0f) continue;
        for (unsigned row = 0u; row < 3u; ++row) turn_level(&m[row * 4u], &m[row * 4u + 2u], turn);
        float x = m[12] - p[0], z = m[14] - p[2];
        turn_level(&x, &z, turn);
        m[12] = p[0] + x + placed[actor].offset[0];
        m[14] = p[2] + z + placed[actor].offset[1];
        recomp_guest_store(address, m, sizeof m);
    }
}

void recomp_actor_pose_place(float right, float forward, float heading, float turn)
{
    const uint32_t actor = pose.actor;
    if (!active(actor)) return;
    /* Along the ground, relative to the camera (free_camera.c): its level
       forward is (sin, cos) of its heading, its right (-cos, sin). */
    const float c = cosf(heading), s = sinf(heading);
    placed[actor].offset[0] += right * -c + forward * s;
    placed[actor].offset[1] += right * s + forward * c;
    placed[actor].turn = remainderf(placed[actor].turn + turn, 6.28318531f);
    placed[actor].set = true;
    placement_dirty = true;
}

void recomp_actor_pose_place_reset(bool all)
{
    for (uint32_t actor = 0u; actor < ACTOR_COUNT; ++actor)
        if (all || actor == pose.actor) {
            memset(&placed[actor], 0, sizeof placed[actor]);
            held_pose[actor] = -1;
        }
    placement_dirty = true;
}

/* A held pose replaces a just-built skeleton, around her hips as built. */
static void hold_bones(uint32_t actor)
{
    const int slot = held_pose[actor];
    if (slot < 0 || !library[slot].used) return;
    if (library[slot].type != *recomp_memory(root_of(actor) + 1u, 1u)) return;
    float bones[RECOMP_POSE_BONES][16];
    recomp_guest_load(bones, bone_of(actor, 0u), sizeof bones);
    recomp_pose_put_back(&library[slot], bones[0], bones);
    recomp_guest_store(bone_of(actor, 0u), bones, sizeof bones);
}

bool recomp_actor_pose_keep_pose(unsigned slot)
{
    const uint32_t actor = pose.actor;
    if (slot >= RECOMP_POSE_SLOTS || !active(actor)) return false;
    float bones[RECOMP_POSE_BONES][16];
    recomp_guest_load(bones, bone_of(actor, 0u), sizeof bones);
    RecompPoseSlot kept;
    if (!recomp_pose_keep(bones, *recomp_memory(root_of(actor) + 1u, 1u), &kept)) return false;
    library[slot] = kept;
    return true;
}

void recomp_actor_pose_hold_pose(unsigned slot, bool hold)
{
    const uint32_t actor = pose.actor;
    if (actor >= ACTOR_COUNT) return;
    held_pose[actor] = hold && slot < RECOMP_POSE_SLOTS ? (int)slot : -1;
    placement_dirty = true;
}

int recomp_actor_pose_held_pose(void)
{
    return pose.actor < ACTOR_COUNT ? held_pose[pose.actor] : -1;
}

const RecompPoseSlot *recomp_actor_pose_library(void)
{
    return library;
}

void recomp_actor_pose_set_library(const RecompPoseSlot from[RECOMP_POSE_SLOTS])
{
    memcpy(library, from, sizeof library);
}

unsigned recomp_actor_pose_chosen_type(void)
{
    return active(pose.actor) ? *recomp_memory(root_of(pose.actor) + 1u, 1u) : 0u;
}

unsigned recomp_actor_pose_history_step(int step)
{
    if (history.count == 0u) return 0u;
    int back = (int)history.back - step;
    if (back < 0) back = 0;
    if (back > (int)history.count - 1) back = (int)history.count - 1;
    history.back = (unsigned)back;
    const unsigned index = (history.newest + HISTORY - history.back) % HISTORY;
    for (uint32_t actor = 0u; actor < ACTOR_COUNT; ++actor) {
        /* A frame from before the scene changed may hold another character
           in this place (her type, at +1 of the root block): leave her. */
        if (!active(actor) || history.roots[index][actor][1] != *recomp_memory(root_of(actor) + 1u, 1u)) continue;
        recomp_guest_store(bone_of(actor, 0u), history.bones[index][actor], BONE_COUNT * BONE_SIZE);
        recomp_guest_store(root_of(actor), history.roots[index][actor], ROOT_STRIDE);
    }
    return history.back;
}

unsigned recomp_actor_pose_history_back(void)
{
    return history.back;
}

void recomp_actor_pose_frame(void)
{
    if (!pose.holding) return;
    if (!active(pose.actor)) {
        recomp_actor_pose_release();
        return;
    }
    uint32_t layer = layer_of(pose.actor);
    if (*recomp_memory_u16(layer + LAYER_CLIP) != pose.clip) request(pose.actor, pose.clip);
    recomp_guest_store(root_of(pose.actor) + ROOT_POSITION, pose.position, sizeof pose.position);
}
#else
bool recomp_actor_pose_keep_pose(unsigned slot)
{
    (void)slot;
    return false;
}

void recomp_actor_pose_hold_pose(unsigned slot, bool hold)
{
    (void)slot;
    (void)hold;
}

int recomp_actor_pose_held_pose(void)
{
    return -1;
}

static RecompPoseSlot no_library[RECOMP_POSE_SLOTS];

const RecompPoseSlot *recomp_actor_pose_library(void)
{
    return no_library;
}

void recomp_actor_pose_set_library(const RecompPoseSlot from[RECOMP_POSE_SLOTS])
{
    (void)from;
}

unsigned recomp_actor_pose_chosen_type(void)
{
    return 0u;
}

void recomp_actor_pose_place(float right, float forward, float heading, float turn)
{
    (void)right;
    (void)forward;
    (void)heading;
    (void)turn;
}

void recomp_actor_pose_place_reset(bool all)
{
    (void)all;
}

void recomp_actor_pose_look_while_frozen(void)
{
}

bool recomp_actor_pose_character_world(const float world[16])
{
    (void)world;
    return false;
}

void recomp_actor_pose_set_look(bool on, bool eyes, const float eye[3])
{
    (void)on;
    (void)eyes;
    (void)eye;
}

unsigned recomp_actor_pose_history_step(int step)
{
    (void)step;
    return 0u;
}

unsigned recomp_actor_pose_history_back(void)
{
    return 0u;
}

void recomp_actor_pose_next_actor(void)
{
}

bool recomp_actor_pose_step_clip(int step)
{
    (void)step;
    return false;
}

bool recomp_actor_pose_browse(int step)
{
    (void)step;
    return false;
}

bool recomp_actor_pose_play_browsed(void)
{
    return false;
}

void recomp_actor_pose_release(void)
{
}

void recomp_actor_pose_frame(void)
{
}

bool recomp_actor_pose_focus(float point[3])
{
    (void)point;
    return false;
}

bool recomp_actor_pose_hips(uint32_t actor, float point[3])
{
    (void)actor;
    (void)point;
    return false;
}

bool recomp_actor_pose_facing(float direction[3])
{
    (void)direction;
    return false;
}

void recomp_actor_pose_choose(uint32_t actor)
{
    (void)actor;
}

void recomp_actor_pose_status(RecompActorPoseStatus *status)
{
    memset(status, 0, sizeof *status);
}

void recomp_actor_pose_name(uint32_t actor, char *name, uint32_t size)
{
    if (name != NULL && size != 0u) snprintf(name, size, "Character %u", (unsigned)actor + 1u);
}

void recomp_actor_pose_set_hidden(uint32_t mask)
{
    (void)mask;
}

RecompFunction recomp_actor_pose_lookup_manual(uint32_t guest_address)
{
    (void)guest_address;
    return NULL;
}
#endif
