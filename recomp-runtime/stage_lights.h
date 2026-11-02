#ifndef DOAXBV_RECOMP_STAGE_LIGHTS_H
#define DOAXBV_RECOMP_STAGE_LIGHTS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The scene lights the game draws its characters and props with
   (docs/stage-lights.md). Each scene keeps four game light records, the
   stage's rig, and turns each into a D3D light in a four-slot table that
   0x0017CC50 hands the device; light 0's ambient colour becomes the
   device's ambient render state. Plain data and rules only;
   stage_lights_adapter.c reads and writes guest memory. */

/* A game light record, 0x58 bytes. Angles are binary: 65536 a turn. */
typedef struct RecompStageLight {
    uint32_t kind;               /* +0x00 0 off, 1 directional, 2 point, 3 spot */
    float ambient_alpha;         /* +0x04 */
    float ambient[3];            /* +0x08 */
    float diffuse_alpha;         /* +0x14 */
    float diffuse[3];            /* +0x18 */
    int32_t yaw, pitch;          /* +0x24, +0x28: the direction the light comes from */
    float position[3];           /* +0x2C */
    float attenuation[3];        /* +0x38 */
    float range, falloff;        /* +0x44, +0x48 */
    float theta, phi;            /* +0x4C, +0x50: spot cone */
    float specular_scale;        /* +0x54: specular is the diffuse colour times this */
} RecompStageLight;

enum {
    RECOMP_STAGE_LIGHT_SIZE = 0x58u,
    /* A D3DLIGHT8, then the specular scale and whether the light is on. */
    RECOMP_STAGE_D3D_LIGHT_SIZE = 0x70u,
};

/* Reads a record from its guest bytes. */
void recomp_stage_light_read(const uint8_t bytes[RECOMP_STAGE_LIGHT_SIZE], RecompStageLight *light);
void recomp_stage_light_write(const RecompStageLight *light, uint8_t bytes[RECOMP_STAGE_LIGHT_SIZE]);
/* The D3D light for a record, as 0x0017CB50 writes it. `toward` is the
   direction the light comes from, as 0x0017B150 gives it for (-yaw, pitch);
   the D3D direction is its opposite. */
void recomp_stage_light_to_d3d(const RecompStageLight *light, const float toward[3],
    uint8_t out[RECOMP_STAGE_D3D_LIGHT_SIZE]);
/* The ambient render state for three channels, as 0x0017CE00 packs it:
   opaque, each channel 255 at 1 or more, 0 at 0 or less, else truncated. */
uint32_t recomp_stage_ambient_color(float red, float green, float blue);

#ifdef __cplusplus
}
#endif

#endif
