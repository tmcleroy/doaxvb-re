#ifndef DOAXBV_RECOMP_CAMERA_EFFECTS_SHADER_H
#define DOAXBV_RECOMP_CAMERA_EFFECTS_SHADER_H

/* The camera effects pass (camera_effects.h) every presenter draws: its
   uniforms, how they are worked out from the free camera's effects, and its
   shader, written once and given to Metal (MSL), Vulkan (GLSL) and D3D11
   (HLSL) by swapping a few words. C++, header only. */

#include "camera_effects.h"
#include "d3d_presenter.h"

#include <cmath>
#include <cstring>

#include <algorithm>
#include <string>

namespace recomp_effects {

// Uniforms of the effects pass, in floats.
struct EffectsUniforms {
    float size[4];     // width, height, 1/width, 1/height
    float dof[4];      // on, blur scale (px), focus (world units; 0 = the centre's depth), largest blur (px)
    float taps[4];     // gather taps, -
    float look[4];     // vignette, grain, letterbox aspect (0 = none), frame number
    float grade[3][4]; // colour matrix rows
    float bloom[4];    // amount, reach (px), brightness it starts at, -
    float sun[4];      // sun's place on screen (uv), glare strength, -
};

/* Depth of field gathers along a golden-angle spiral out to the largest blur
   (the single-pass bokeh of Dennis Gustafsson, 2018): each tap counts where its
   own blur reaches the centre, and a tap behind the centre may spread no more
   than the centre does, so a sharp foreground keeps its edge. */
inline constexpr char kEffectsBody[] = R"(
    float2 st = pos.xy * p.size.zw;
    float2 uv = st;
    float frame_aspect = p.size.x * p.size.w;
    if (p.look.z > 0.0) {
        if (p.look.z > frame_aspect) {
            if (abs(uv.y - 0.5) > 0.5 * frame_aspect / p.look.z) return float4(0.0, 0.0, 0.0, 1.0);
        } else if (abs(uv.x - 0.5) > 0.5 * p.look.z / frame_aspect) {
            return float4(0.0, 0.0, 0.0, 1.0);
        }
    }
    float3 rgb = SAMPLE(color_tex, color_smp, st).rgb;
    if (p.dof.x > 0.5) {
        float focus = p.dof.z;
        if (focus <= 0.0) {
            float2 o = 24.0 * p.size.zw;
            focus = min(min(SAMPLE(depth_tex, depth_smp, float2(0.5)).r, SAMPLE(depth_tex, depth_smp, float2(0.5) + o).r),
                        min(SAMPLE(depth_tex, depth_smp, float2(0.5) - o).r,
                            min(SAMPLE(depth_tex, depth_smp, float2(0.5 + o.x, 0.5 - o.y)).r,
                                SAMPLE(depth_tex, depth_smp, float2(0.5 - o.x, 0.5 + o.y)).r)));
        }
        float centre_depth = SAMPLE(depth_tex, depth_smp, st).r;
        float centre_blur = min(p.dof.y * abs(centre_depth - focus) / max(centre_depth, 0.001), p.dof.w);
        float3 total = rgb;
        float count = 1.0;
        // About as many taps as fit within the largest blur: r^2 grows by 2 steps a tap.
        float spacing = max(p.dof.w * p.dof.w / (1.67 * p.taps.x), 0.5);
        float radius = spacing;
        float angle = 0.0;
        for (int i = 0; i < int(p.taps.x) && radius < p.dof.w; ++i) {
            float2 tap = st + float2(cos(angle), sin(angle)) * p.size.zw * radius;
            float3 tap_rgb = SAMPLE(color_tex, color_smp, tap).rgb;
            float tap_depth = SAMPLE(depth_tex, depth_smp, tap).r;
            float tap_blur = min(p.dof.y * abs(tap_depth - focus) / max(tap_depth, 0.001), p.dof.w);
            if (tap_depth > centre_depth) tap_blur = clamp(tap_blur, 0.0, centre_blur * 2.0);
            float m = smoothstep(radius - 0.5, radius + 0.5, tap_blur);
            total += MIX(total / count, tap_rgb, m);
            count += 1.0;
            radius += spacing / radius;
            angle += 2.39996323;
        }
        rgb = total / count;
    }
    // Bloom: the bright parts of the picture around the pixel, nearer ones more.
    if (p.bloom.x > 0.0) {
        float3 glow = float3(0.0);
        float weight = 0.0;
        for (int i = 0; i < 24; ++i) {
            float r = p.bloom.y * sqrt((float(i) + 0.5) / 24.0);
            float a = float(i) * 2.39996323;
            float3 c = SAMPLE(color_tex, color_smp, st + float2(cos(a), sin(a)) * r * p.size.zw).rgb;
            float w = 1.0 - float(i) / 24.0;
            glow += c * smoothstep(p.bloom.z, 1.0, max(max(c.r, c.g), c.b)) * w;
            weight += w;
        }
        // Screen blend: the glow lifts toward white without clipping what is already bright.
        float3 lift = clamp(p.bloom.x * 1.5 * glow / weight, 0.0, 1.0);
        rgb = 1.0 - (1.0 - rgb) * (1.0 - lift);
    }
    // Sun glare: a glow round the sun, when nothing near covers it, and ghosts
    // along the line from it through the middle of the picture.
    if (p.sun.z > 0.0 && SAMPLE(depth_tex, depth_smp, p.sun.xy).r > 500.0) {
        float2 d = (st - p.sun.xy) * float2(frame_aspect, 1.0);
        float3 flare = float3(1.0, 0.85, 0.6) * (exp(-length(d) * 6.0) * 0.5 + exp(-length(d) * 30.0) * 0.8);
        float2 axis = float2(0.5) - p.sun.xy;
        for (int g = 1; g <= 4; ++g) {
            float2 at = p.sun.xy + axis * (0.6 * float(g));
            float gd = length((st - at) * float2(frame_aspect, 1.0));
            float size = 0.02 + 0.015 * float(g);
            flare += float3(0.5, 0.6, 1.0) * 0.12 * (1.0 - smoothstep(size * 0.6, size, gd)) / float(g);
        }
        rgb += p.sun.z * flare;
    }
    rgb = float3(dot(p.grade[0].xyz, rgb) + p.grade[0].w, dot(p.grade[1].xyz, rgb) + p.grade[1].w,
                 dot(p.grade[2].xyz, rgb) + p.grade[2].w);
    if (p.look.x > 0.0) {
        float2 v = (uv - 0.5) * float2(frame_aspect, 1.0);
        float reach = length(v) / length(float2(frame_aspect, 1.0) * 0.5);
        rgb *= 1.0 - p.look.x * smoothstep(0.35, 1.05, reach);
        // Half a step of noise keeps the darkening from banding in a clear sky.
        rgb += (FRACT(sin(dot(pos.xy, float2(91.7, 37.3))) * 24634.63) - 0.5) / 255.0;
    }
    if (p.look.y > 0.0) {
        float n = FRACT(sin(dot(pos.xy + p.look.w * float2(12.9898, 4.1414), float2(12.9898, 78.233))) * 43758.5453);
        rgb += p.look.y * 0.14 * (n - 0.5);
    }
    // Transparent shots keep the characters: alpha from the mask.
    return float4(clamp(rgb, 0.0, 1.0), p.taps.y > 0.5 ? SAMPLE(depth_tex, depth_smp, st).g : 1.0);
)";

inline std::string effectsSource(bool msl)
{
    std::string body = kEffectsBody;
    const auto swap = [&body](const std::string &from, const std::string &to) {
        for (size_t at = body.find(from); at != std::string::npos; at = body.find(from, at + to.size()))
            body.replace(at, from.size(), to);
    };
    if (msl) {
        swap("SAMPLE(color_tex, color_smp, ", "color_tex.sample(color_smp, ");
        swap("SAMPLE(depth_tex, depth_smp, ", "depth_tex.sample(depth_smp, ");
        swap("MIX(", "mix(");
        swap("FRACT(", "fract(");
        return "#include <metal_stdlib>\nusing namespace metal;\n"
            "struct Effects { float4 size; float4 dof; float4 taps; float4 look; float4 grade[3]; float4 bloom; float4 sun; };\n"
            "struct VOut { float4 position [[position]]; };\n"
            "vertex VOut vs_main(uint id [[vertex_id]]) {\n"
            "    VOut out;\n"
            "    float2 p = float2((id << 1) & 2, id & 2);\n"
            "    out.position = float4(p * 2.0 - 1.0, 0.0, 1.0);\n"
            "    return out;\n}\n"
            "fragment float4 ps_main(VOut in [[stage_in]], constant Effects &p [[buffer(0)]],\n"
            "                        texture2d<float> color_tex [[texture(0)]], sampler color_smp [[sampler(0)]],\n"
            "                        texture2d<float> depth_tex [[texture(1)]], sampler depth_smp [[sampler(1)]]) {\n"
            "    float4 pos = in.position;\n" + body + "}\n";
    }
    swap("SAMPLE(color_tex, color_smp, ", "texture(color_tex, ");
    swap("SAMPLE(depth_tex, depth_smp, ", "texture(depth_tex, ");
    swap("MIX(", "mix(");
    swap("FRACT(", "fract(");
    swap("float2", "vec2");
    swap("float3", "vec3");
    swap("float4", "vec4");
    return "#version 450\n"
        "layout(set = 3, binding = 0, std140) uniform Effects { vec4 size; vec4 dof; vec4 taps; vec4 look; vec4 grade[3]; vec4 bloom; vec4 sun; } p;\n"
        "layout(set = 2, binding = 0) uniform sampler2D color_tex;\n"
        "layout(set = 2, binding = 1) uniform sampler2D depth_tex;\n"
        "layout(location = 0) out vec4 out_color;\n"
        "vec4 effects(vec4 pos) {\n" + body + "}\n"
        "void main() { out_color = effects(gl_FragCoord); }\n";
}


/* HLSL (Shader Model 4): the same body, with a full-screen triangle. A
   vector built from one value is a cast in HLSL, and the picture is read at
   its one level (SampleLevel): fxc will not take gradients in a loop of
   varying length. */
inline std::string effectsHlsl()
{
    std::string body = kEffectsBody;
    const auto swap = [&body](const std::string &from, const std::string &to) {
        for (size_t at = body.find(from); at != std::string::npos; at = body.find(from, at + to.size()))
            body.replace(at, from.size(), to);
    };
    swap("SAMPLE(color_tex, color_smp, ", "COLOR_AT(");
    swap("SAMPLE(depth_tex, depth_smp, ", "DEPTH_AT(");
    swap("MIX(", "lerp(");
    swap("FRACT(", "frac(");
    swap("float3(0.0)", "((float3)0.0)");
    swap("float2(0.5)", "((float2)0.5)");
    return "cbuffer Effects : register(b0) { float4 u_size; float4 u_dof; float4 u_taps; float4 u_look; float4 u_grade[3]; float4 u_bloom; float4 u_sun; };\n"
        "struct EffectsP { float4 size; float4 dof; float4 taps; float4 look; float4 grade[3]; float4 bloom; float4 sun; };\n"
        "Texture2D color_tex : register(t0);\n"
        "Texture2D depth_tex : register(t1);\n"
        "SamplerState color_smp : register(s0);\n"
        "SamplerState depth_smp : register(s1);\n"
        "#define COLOR_AT(uv) color_tex.SampleLevel(color_smp, (uv), 0)\n"
        "#define DEPTH_AT(uv) depth_tex.SampleLevel(depth_smp, (uv), 0)\n"
        "float4 vs_main(uint id : SV_VertexID) : SV_Position {\n"
        "    float2 q = float2((id << 1) & 2, id & 2);\n"
        "    return float4(q * 2.0 - 1.0, 0.0, 1.0);\n}\n"
        "float4 effects(float4 pos, EffectsP p) {\n" + body + "}\n"
        "float4 ps_main(float4 pos : SV_Position) : SV_Target {\n"
        "    EffectsP p;\n"
        "    p.size = u_size; p.dof = u_dof; p.taps = u_taps; p.look = u_look;\n"
        "    p.grade[0] = u_grade[0]; p.grade[1] = u_grade[1]; p.grade[2] = u_grade[2];\n"
        "    p.bloom = u_bloom; p.sun = u_sun;\n"
        "    return effects(pos, p);\n}\n";
}

/* What a presenter knows of its frame for the effects. */
struct EffectsFrame {
    float width = 0.0f, height = 0.0f;
    /* A big screenshot's frame is this many times larger (1 otherwise). */
    float bigger = 1.0f;
    /* The view depth was drawn this frame; without it there is no depth of field. */
    bool depth_drawn = false;
    /* The sun's direction and the scene camera it was seen through. */
    bool sun_seen = false;
    float sun_dir[3] = {}, sun_camera[16] = {};
    unsigned frame = 0;
};

/* The uniforms for these effects on this frame; `settings.focus` already
   resolved (a subject's depth in place of RECOMP_CAMERA_FOCUS_SUBJECT). */
inline EffectsUniforms effectsUniforms(const RecompCameraEffects &settings, const EffectsFrame &frame)
{
    const float width = frame.width, height = frame.height;
    const float focus = settings.focus;
    EffectsUniforms u{};
    u.size[0] = width; u.size[1] = height; u.size[2] = 1.0f / width; u.size[3] = 1.0f / height;
    const float largest = std::max(4.0f, height * 0.016f);
    // A big screenshot's frame is n times larger: n^2 times the taps keep its blur as smooth.
    u.taps[0] = std::min(96.0f * frame.bigger * frame.bigger, 1536.0f);
    u.taps[1] = settings.transparent ? 1.0f : 0.0f;
    u.dof[0] = settings.depth_of_field && frame.depth_drawn ? 1.0f : 0.0f;
    // An automatic focus scales the blur as if focused at 2.5 units.
    u.dof[1] = recomp_camera_effects_blur_scale(&settings, focus > 0.0f ? focus : 2.5f, height);
    u.dof[2] = focus;
    u.dof[3] = largest;
    u.look[0] = settings.vignette;
    u.look[1] = settings.grain;
    u.look[2] = settings.letterbox;
    u.look[3] = float(frame.frame % 1024u);
    recomp_camera_effects_filter_matrix(settings.filter, u.grade);
    recomp_camera_effects_adjust_matrix(&settings, u.grade);
    if (settings.bloom > 0.0f) {
        u.bloom[0] = settings.bloom;
        u.bloom[1] = height * 0.03f;
        u.bloom[2] = 0.88f;
        // The sun's place on screen: its direction through the scene's camera.
        if (frame.sun_seen) {
            float clip[4];
            for (unsigned c = 0; c < 4; ++c)
                clip[c] = frame.sun_dir[0] * frame.sun_camera[c] + frame.sun_dir[1] * frame.sun_camera[4 + c] +
                    frame.sun_dir[2] * frame.sun_camera[8 + c];
            if (clip[3] > 1e-4f) {
                const float x = clip[0] / clip[3] * 0.5f + 0.5f, y = -clip[1] / clip[3] * 0.5f + 0.5f;
                if (x > 0.0f && x < 1.0f && y > 0.0f && y < 1.0f) {
                    u.sun[0] = x;
                    u.sun[1] = y;
                    u.sun[2] = settings.bloom;
                }
            }
        }
    }
    return u;
}

/* What one draw tells the effects: the sun's direction and the camera it
   was seen through (the first lit scenery draw with the game's shadow
   direction), and the subject: of the characters' pieces, the one whose
   middle is nearest the middle of the picture gives its view depth. Each
   piece's middle is the average of up to 24 of the vertices its indices use,
   through the same per-bone transforms the vertex shader blends. */
inline void observeDraw(const RecompD3dPresenterDrawCommand &draw, bool &sun_seen, float sun_dir[3],
    float sun_camera[16], float &subject_offset, float &subject_depth)
{
    if (!sun_seen && draw.sun[3] == 1.0f && draw.directional.enabled && !draw.blend_weight_count) {
        sun_seen = true;
        std::memcpy(sun_dir, draw.sun, 3 * sizeof(float));
        std::memcpy(sun_camera, draw.view_projection, 16 * sizeof(float));
    }
    if (!draw.character || draw.blend_weight_count > 3 || !draw.index_count || !draw.has_transform ||
        draw.vertex_stride < (3u + draw.blend_weight_count) * sizeof(float)) return;
    const uint32_t samples = std::min(draw.index_count, 24u);
    const auto *indices = static_cast<const uint16_t *>(draw.index_bytes);
    float clip[4] = {};
    for (uint32_t k = 0; k < samples; ++k) {
        const uint32_t v = indices[k * draw.index_count / samples];
        if (v >= draw.vertex_count) return;
        float vertex[6]{};
        std::memcpy(vertex, static_cast<const uint8_t *>(draw.vertex_bytes) + size_t(v) * draw.vertex_stride,
            (3u + draw.blend_weight_count) * sizeof(float));
        float remainder = 1.0f;
        for (uint32_t b = 0; b <= draw.blend_weight_count; ++b) {
            const float weight = b == draw.blend_weight_count ? remainder : vertex[3 + b];
            remainder -= weight;
            const float *m = b == 0 ? draw.transform : draw.blend_transforms[b - 1];
            for (unsigned c = 0; c < 4; ++c)
                clip[c] += weight * (vertex[0] * m[c] + vertex[1] * m[4 + c] + vertex[2] * m[8 + c] + m[12 + c]);
        }
    }
    if (!(clip[3] > 0.05f * samples) || !std::isfinite(clip[3])) return;
    const float x = clip[0] / clip[3], y = clip[1] / clip[3];
    const float offset = std::sqrt(x * x + y * y);
    if (!std::isfinite(offset) || std::fabs(x) > 1.0f || std::fabs(y) > 1.0f) return;
    if (subject_depth > 0.0f && offset >= subject_offset) return;
    subject_offset = offset;
    subject_depth = clip[3] / samples;
}

} // namespace recomp_effects

#endif
