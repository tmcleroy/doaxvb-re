#include "stage_lights.h"

#include <string.h>

/* The game's light kinds as D3D light types (table at 0x002870E0). */
static const uint32_t D3D_TYPES[4] = {0u, 3u, 1u, 2u};

void recomp_stage_light_read(const uint8_t bytes[RECOMP_STAGE_LIGHT_SIZE], RecompStageLight *light)
{
    memcpy(&light->kind, bytes + 0x00, 4);
    memcpy(&light->ambient_alpha, bytes + 0x04, 4);
    memcpy(light->ambient, bytes + 0x08, 12);
    memcpy(&light->diffuse_alpha, bytes + 0x14, 4);
    memcpy(light->diffuse, bytes + 0x18, 12);
    memcpy(&light->yaw, bytes + 0x24, 4);
    memcpy(&light->pitch, bytes + 0x28, 4);
    memcpy(light->position, bytes + 0x2C, 12);
    memcpy(light->attenuation, bytes + 0x38, 12);
    memcpy(&light->range, bytes + 0x44, 4);
    memcpy(&light->falloff, bytes + 0x48, 4);
    memcpy(&light->theta, bytes + 0x4C, 4);
    memcpy(&light->phi, bytes + 0x50, 4);
    memcpy(&light->specular_scale, bytes + 0x54, 4);
}

void recomp_stage_light_write(const RecompStageLight *light, uint8_t bytes[RECOMP_STAGE_LIGHT_SIZE])
{
    memcpy(bytes + 0x00, &light->kind, 4);
    memcpy(bytes + 0x04, &light->ambient_alpha, 4);
    memcpy(bytes + 0x08, light->ambient, 12);
    memcpy(bytes + 0x14, &light->diffuse_alpha, 4);
    memcpy(bytes + 0x18, light->diffuse, 12);
    memcpy(bytes + 0x24, &light->yaw, 4);
    memcpy(bytes + 0x28, &light->pitch, 4);
    memcpy(bytes + 0x2C, light->position, 12);
    memcpy(bytes + 0x38, light->attenuation, 12);
    memcpy(bytes + 0x44, &light->range, 4);
    memcpy(bytes + 0x48, &light->falloff, 4);
    memcpy(bytes + 0x4C, &light->theta, 4);
    memcpy(bytes + 0x50, &light->phi, 4);
    memcpy(bytes + 0x54, &light->specular_scale, 4);
}

static void put(uint8_t *out, unsigned offset, float value)
{
    memcpy(out + offset, &value, 4);
}

void recomp_stage_light_to_d3d(const RecompStageLight *light, const float toward[3],
    uint8_t out[RECOMP_STAGE_D3D_LIGHT_SIZE])
{
    const uint32_t type = D3D_TYPES[light->kind & 3u];
    const uint32_t on = light->kind != 0u;
    memcpy(out + 0x00, &type, 4);
    /* Diffuse, then specular: the diffuse colour times the scale, as the
       game multiplies on the x87 at double precision. */
    for (unsigned c = 0u; c < 3u; ++c) {
        put(out, 0x04 + 4u * c, light->diffuse[c]);
        put(out, 0x14 + 4u * c, (float)((double)light->specular_scale * (double)light->diffuse[c]));
        put(out, 0x24 + 4u * c, light->ambient[c]);
        put(out, 0x34 + 4u * c, light->position[c]);
        put(out, 0x40 + 4u * c, -toward[c]);
        put(out, 0x54 + 4u * c, light->attenuation[c]);
    }
    put(out, 0x10, light->diffuse_alpha);
    put(out, 0x20, light->diffuse_alpha);
    put(out, 0x30, light->ambient_alpha);
    put(out, 0x4C, light->range);
    put(out, 0x50, light->falloff);
    put(out, 0x60, light->theta);
    put(out, 0x64, light->phi);
    put(out, 0x68, light->specular_scale);
    memcpy(out + 0x6C, &on, 4);
}

static uint32_t channel(float value)
{
    if (!(value < 1.0f)) return 0xFFu;
    if (!(value > 0.0f)) return 0u;
    /* _ftol truncates toward zero. */
    return (uint32_t)(int32_t)((double)value * 255.0);
}

uint32_t recomp_stage_ambient_color(float red, float green, float blue)
{
    return 0xFF000000u | channel(red) << 16 | channel(green) << 8 | channel(blue);
}
