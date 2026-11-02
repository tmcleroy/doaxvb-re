#include "stage_lights_adapter.h"
#include "stage_lights.h"
#include "time_of_day.h"
#include "camera_effects.h"
#include "viewer_scene.h"
#include "d3d_frame_adapter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Scene lights (docs/stage-lights.md).

   Every scene's frame code copies its stage rig, four game light records of
   0x58 bytes at 0x009D4F58, to 0x009D5240 and calls 0x0017CB50 once a light
   (EDI the record, the stack argument its slot 0 to 3). That fills slot n of
   the D3D light table at 0x00A243F0 + n * 0x70 (stage_lights.h). It then
   calls 0x0017CE00 with light 0's ambient colour (three floats on the stack),
   which packs the ambient render state into 0x00A243E4, and 0x0017CC50,
   which hands the table and the ambient colour to the device.

   0x0017CB50 asks 0x0017B150 for the light's direction from its two binary
   angles (-yaw, pitch); that and the sine and cosine it calls (0x0017AB70, a
   library polynomial) stay generated, called here as they are by the game. */
enum {
    LIGHT_TABLE = 0x00A243F0u,
    AMBIENT = 0x00A243E4u,
    LIGHT_TO_D3D = 0x0017CB50u,
    PACK_AMBIENT = 0x0017CE00u,
    ANGLES_TO_DIRECTION = 0x0017B150u,
    SLOTS = 4u,
};

#ifdef RECOMP_FULL_PROGRAM
void sub_0017CB50(void);
void sub_0017CE00(void);

/* 0x0017B150 with (-yaw, pitch) on the stack writes the direction the light
   comes from to [ESI]; registers and the FPU are left as they were. */
static void toward_from_angles(int32_t yaw, int32_t pitch, float toward[3])
{
    const RecompRegisters registers = recomp_runtime.registers;
    RecompFpuContext fpu;
    recomp_fpu_context_save(&fpu);
    const uint32_t buffer = recomp_runtime.registers.esp - 0x40u;
    recomp_runtime.registers.esp = buffer - 12u;
    *recomp_memory_u32(recomp_runtime.registers.esp) = 0u;
    *recomp_memory_u32(recomp_runtime.registers.esp + 4u) = (uint32_t)-yaw;
    *recomp_memory_u32(recomp_runtime.registers.esp + 8u) = (uint32_t)pitch;
    recomp_runtime.registers.esi = buffer;
    recomp_dispatch(ANGLES_TO_DIRECTION);
    recomp_guest_load(toward, buffer, 3u * sizeof(float));
    recomp_runtime.registers = registers;
    recomp_fpu_context_restore(&fpu);
}

/* RECOMP_STAGE_LIGHTS_DUMP=1 logs each slot's record whenever it changes,
   with the sun direction the shadows use (0x00333870), so the lights of each
   stage and time of day can be read off a run. */
static void dump_light(uint32_t record, uint32_t slot, const uint8_t bytes[RECOMP_STAGE_LIGHT_SIZE],
    const RecompStageLight *light)
{
    static int dump = -1;
    static struct { uint32_t record; uint8_t bytes[RECOMP_STAGE_LIGHT_SIZE]; } seen[64];
    static unsigned count;
    if (dump < 0) dump = getenv("RECOMP_STAGE_LIGHTS_DUMP") != NULL;
    if (!dump) return;
    static float last_sun[3];
    float now_sun[3];
    recomp_guest_load(now_sun, 0x00333870u, sizeof now_sun);
    if (memcmp(now_sun, last_sun, sizeof now_sun) != 0) {
        memcpy(last_sun, now_sun, sizeof now_sun);
        fprintf(stderr, "stage sun at frame %u: %.4f,%.4f,%.4f\n", recomp_d3d_frame_adapter_swap_counter(),
            now_sun[0], now_sun[1], now_sun[2]);
    }
    for (unsigned i = 0u; i < count; ++i)
        if (seen[i].record == record && memcmp(seen[i].bytes, bytes, RECOMP_STAGE_LIGHT_SIZE) == 0) return;
    if (count < 64u) {
        seen[count].record = record;
        memcpy(seen[count++].bytes, bytes, RECOMP_STAGE_LIGHT_SIZE);
    }
    float sun[3];
    recomp_guest_load(sun, 0x00333870u, sizeof sun);
    fprintf(stderr, "stage light frame %u %08X slot %u: kind=%u ambient=%.4f,%.4f,%.4f/%.3f diffuse=%.4f,%.4f,%.4f/%.3f "
        "yaw=%d pitch=%d pos=%.2f,%.2f,%.2f att=%.3f,%.3f,%.3f range=%.1f spec=%.3f sun=%.4f,%.4f,%.4f\n",
        recomp_d3d_frame_adapter_swap_counter(), record, slot, light->kind, light->ambient[0], light->ambient[1], light->ambient[2], light->ambient_alpha,
        light->diffuse[0], light->diffuse[1], light->diffuse[2], light->diffuse_alpha, light->yaw, light->pitch,
        light->position[0], light->position[1], light->position[2], light->attenuation[0], light->attenuation[1],
        light->attenuation[2], light->range, light->specular_scale, sun[0], sun[1], sun[2]);
}

/* Time of day (time_of_day.h): while the free camera flies with a time
   chosen, each light leaves here relit for that time. Light 0 comes first
   each frame; its colours tell the time the stage was made for, and its new
   direction becomes the sun the shadows follow (0x00333870), put back when
   the time goes back to the stage's own. */
/* Radians: about 25 degrees. */
static const float LIGHT_ELEVATION = 0.45f;

static struct {
    bool on;
    float saved_sun[3];
    RecompTimeOfDayLook stage, target;
} day;

/* The loaded court's stage (COURT_LOADED[COURT_VIEW], as cpu_exhibition.c
   reads it) tells the time the scene was made for; others count as day.
   A match changes rigs between shots, so the lights cannot tell it. */
static float stage_hour(void)
{
    const uint8_t view = *recomp_memory(0x004D56E4u, 1u);
    const uint8_t stage = view < 8u ? *recomp_memory(0x004D92F0u + view, 1u) : 0u;
    switch (recomp_viewer_stage_time(stage)) {
    case RECOMP_VIEWER_TIME_SUNSET: return recomp_time_of_day_game_hour(1u);
    case RECOMP_VIEWER_TIME_DUSK: return recomp_time_of_day_game_hour(2u);
    default: return recomp_time_of_day_game_hour(0u);
    }
}

/* Turns a vector about the vertical by `angle` radians. */
static void turn_about_vertical(float v[3], float angle)
{
    const float c = cosf(angle), sn = sinf(angle), x = v[0], z = v[2];
    v[0] = c * x + sn * z;
    v[2] = -sn * x + c * z;
}

/* The free camera's time of day and key light (camera_effects.h) on one
   light; light 0 comes first each frame. Light 0 also sets the sun the
   shadows follow; the game's own is put back when neither is set. */
static void time_of_day_light(uint32_t slot, RecompStageLight *light, float toward[3])
{
    RecompCameraEffects effects;
    recomp_camera_effects_take(&effects);
    const bool timed = effects.hour >= 0.0f;
    const bool keyed = effects.light_angle != 0.0f || (effects.light_strength > 0.0f && effects.light_strength != 1.0f);
    if (!timed && !keyed) {
        if (day.on) {
            recomp_guest_store(0x00333870u, day.saved_sun, sizeof day.saved_sun);
            recomp_camera_effects_report_scene_tint(false, NULL);
            day.on = false;
        }
        return;
    }
    if (!day.on) {
        recomp_guest_load(day.saved_sun, 0x00333870u, sizeof day.saved_sun);
        day.on = true;
    }
    if (slot == 0u) {
        if (timed) {
            recomp_time_of_day_look(stage_hour(), &day.stage);
            recomp_time_of_day_look(effects.hour, &day.target);
            float tint[3];
            recomp_time_of_day_tint(&day.stage, &day.target, tint);
            recomp_camera_effects_report_scene_tint(true, tint);
        } else {
            recomp_camera_effects_report_scene_tint(false, NULL);
        }
    }
    if (timed) recomp_time_of_day_relight(&day.stage, &day.target, slot, light);
    if (slot != 0u || light->kind == 0u) return;
    /* Shadows follow the true sun; the key light stays at least
       LIGHT_ELEVATION up, or flat ground would go dark at sunset. */
    float sun[3], aimed[3];
    if (timed) {
        recomp_time_of_day_direction(toward, day.target.elevation, sun);
        const float lit = day.target.elevation > LIGHT_ELEVATION ? day.target.elevation : LIGHT_ELEVATION;
        recomp_time_of_day_direction(toward, lit, aimed);
    } else {
        memcpy(sun, day.saved_sun, sizeof sun);
        memcpy(aimed, toward, sizeof aimed);
    }
    turn_about_vertical(sun, effects.light_angle);
    turn_about_vertical(aimed, effects.light_angle);
    recomp_guest_store(0x00333870u, sun, sizeof sun);
    memcpy(toward, aimed, sizeof aimed);
    if (effects.light_strength > 0.0f)
        for (unsigned c = 0u; c < 3u; ++c) light->diffuse[c] *= effects.light_strength;
}

/* 0x0017CB50: EDI the game record, [ESP+4] the slot (ESP points at the
   return slot the caller pushed, which returning pops). As the game leaves
   them: EAX whether the light is on, ECX the record's kind, EDX its
   specular scale. */
static void light_to_d3d(void)
{
    const uint32_t record = recomp_runtime.registers.edi;
    const uint32_t slot = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    uint8_t bytes[RECOMP_STAGE_LIGHT_SIZE], out[RECOMP_STAGE_D3D_LIGHT_SIZE];
    RecompStageLight light;
    float toward[3];
    recomp_guest_load(bytes, record, sizeof bytes);
    recomp_stage_light_read(bytes, &light);
    dump_light(record, slot, bytes, &light);
    toward_from_angles(light.yaw, light.pitch, toward);
    time_of_day_light(slot, &light, toward);
    recomp_stage_light_to_d3d(&light, toward, out);
    recomp_guest_store(LIGHT_TABLE + (uint32_t)((int32_t)slot * 0x70), out, sizeof out);
    recomp_runtime.registers.eax = light.kind != 0u;
    recomp_runtime.registers.ecx = light.kind;
    memcpy(&recomp_runtime.registers.edx, &light.specular_scale, 4);
    recomp_runtime.registers.esp += 4u; /* ret: the caller pops the slot */
}

/* 0x0017CE00: three floats on the stack, red first. EAX is left as the
   blue channel's byte, as the game leaves it. */
static void pack_ambient(void)
{
    float rgb[3];
    recomp_guest_load(rgb, recomp_runtime.registers.esp + 4u, sizeof rgb);
    const uint32_t color = recomp_stage_ambient_color(rgb[0], rgb[1], rgb[2]);
    recomp_guest_store(AMBIENT, &color, sizeof color);
    recomp_runtime.registers.eax = color & 0xFFu;
    recomp_runtime.registers.esp += 4u; /* ret: the caller pops the colour */
}

/* RECOMP_STAGE_LIGHTS_VERIFY=1: run the generated code, keep what it wrote,
   put memory back, run the readable code, and compare. */
static bool verifying(void)
{
    static int verify = -1;
    if (verify < 0) verify = getenv("RECOMP_STAGE_LIGHTS_VERIFY") != NULL;
    return verify != 0;
}

static void verify_call(RecompFunction generated, RecompFunction readable, uint32_t address,
    uint32_t size, const char *name)
{
    static unsigned long long calls[2], differing[2];
    const int which = readable == pack_ambient;
    uint8_t before[RECOMP_STAGE_D3D_LIGHT_SIZE], theirs[RECOMP_STAGE_D3D_LIGHT_SIZE], mine[RECOMP_STAGE_D3D_LIGHT_SIZE];
    const RecompRegisters registers = recomp_runtime.registers;
    RecompFpuContext fpu;
    recomp_fpu_context_save(&fpu);
    recomp_guest_load(before, address, size);
    generated();
    const RecompRegisters after = recomp_runtime.registers;
    recomp_guest_load(theirs, address, size);
    recomp_guest_store(address, before, size);
    recomp_runtime.registers = registers;
    recomp_fpu_context_restore(&fpu);
    readable();
    recomp_guest_load(mine, address, size);
    ++calls[which];
    const bool same_registers = after.eax == recomp_runtime.registers.eax &&
        after.esp == recomp_runtime.registers.esp && after.ebx == recomp_runtime.registers.ebx &&
        after.esi == recomp_runtime.registers.esi && after.edi == recomp_runtime.registers.edi;
    if (memcmp(theirs, mine, size) != 0 || !same_registers) {
        if (differing[which]++ < 8u) {
            fprintf(stderr, "stage lights verify: %s differs (call %llu, registers %s)\n", name,
                calls[which], same_registers ? "same" : "differ");
            for (uint32_t i = 0u; i < size; i += 4u) {
                uint32_t a, b;
                memcpy(&a, theirs + i, 4);
                memcpy(&b, mine + i, 4);
                if (a != b) fprintf(stderr, "  +0x%02X generated %08X readable %08X\n", i, a, b);
            }
        }
    }
    if (calls[which] % 600u == 0u)
        fprintf(stderr, "stage lights verify: %s %llu calls, %llu differing\n", name, calls[which], differing[which]);
}

static void light_to_d3d_entry(void)
{
    if (!verifying()) {
        light_to_d3d();
        return;
    }
    const uint32_t slot = *recomp_memory_u32(recomp_runtime.registers.esp + 4u);
    verify_call(sub_0017CB50, light_to_d3d, LIGHT_TABLE + slot * 0x70u, RECOMP_STAGE_D3D_LIGHT_SIZE, "light");
}

static void pack_ambient_entry(void)
{
    if (verifying()) verify_call(sub_0017CE00, pack_ambient, AMBIENT, 4u, "ambient");
    else pack_ambient();
}

RecompFunction recomp_stage_lights_lookup_manual(uint32_t guest_address)
{
    /* Seams: the game's light record to D3D light (0x0017CB50) and ambient
       packing (0x0017CE00), manual call targets in the recipe. */
    if (guest_address == LIGHT_TO_D3D) return light_to_d3d_entry;
    if (guest_address == PACK_AMBIENT) return pack_ambient_entry;
    return NULL;
}
#else
RecompFunction recomp_stage_lights_lookup_manual(uint32_t guest_address)
{
    (void)guest_address;
    return NULL;
}
#endif
