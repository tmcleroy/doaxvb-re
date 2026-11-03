#ifndef DOAXBV_RECOMP_D3D_PRESENTER_D3D11_CAMERA_H
#define DOAXBV_RECOMP_D3D_PRESENTER_D3D11_CAMERA_H

/* The free camera's effects on the D3D11 presenter (docs/free-camera.md):
   the effects pass (camera_effects_shader.h) with depth of field, bloom and
   glare, studio backdrops, transparent screenshots, motion blur and video
   recording. With the free camera on, the draw shaders also write a second
   target, as the SDL GPU presenter's do: view depth, 1 where a character
   draws, and the draw's alpha (cameraDepthShaderSource). Big screenshots are
   not drawn here: this presenter's targets are sized once. Included by
   d3d_presenter_d3d11.cpp only; everything fails soft, leaving the frame as
   the game drew it.

   Written without a Windows machine to run it on: it compiles, and its
   shaders compile with glslang's HLSL front end. */

#include "camera_effects.h"
#include "camera_effects_shader.h"
#include "d3d_presenter.h"
#include "screenshot.h"
#include "video_sound.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

/* The draw shaders write the camera's view-depth target: set once, before
   any draw shader is built, when the free camera is on and the g-buffer
   (which takes the same second target) is not. */
inline bool g_d3d11_camera_depth = false;

/* Adds the second target to a draw shader: the view depth (clip w, from the
   vertex shader), whether the draw is a character's (camera_flags.x), and
   the draw's alpha, which blends a hair tip's share into the mask. */
inline bool cameraDepthShaderSource(std::string &source)
{
    const std::string fields = "struct VSOut {";
    const auto at = source.find(fields);
    const auto pixel = source.find("float4 ps_main(");
    if (at == std::string::npos || pixel == std::string::npos) return false;
    const auto signature_end = source.find(") : SV_TARGET", pixel);
    const std::string last = "    return shaded;\n}";
    const auto ending = source.rfind(last);
    if (signature_end == std::string::npos || ending == std::string::npos || ending < signature_end) return false;
    source.replace(ending, last.size(),
        "    shaded.rgb *= camera_flags.yzw;\n"
        "    camera_depth = float4(input.camera_view_depth, camera_flags.x, 0, saturate(shaded.a));\n"
        "    return shaded;\n}");
    const auto body = source.find('{', signature_end);
    source.insert(body + 1, "\n    camera_depth = 0;");
    source.insert(signature_end, ", out float4 camera_depth : SV_Target1");
    for (size_t found = source.find("return output;"); found != std::string::npos && found < source.find("float4 ps_main(");
         found = source.find("return output;", found + 64)) {
        source.insert(found, "output.camera_view_depth = output.position.w; ");
    }
    source.insert(source.find(fields) + fields.size(), "\n    float camera_view_depth : TEXCOORD9;\n");
    return true;
}

// A half float's value (the view depth is kept in half floats).
inline float halfValue(uint16_t h)
{
    const uint32_t sign = uint32_t(h >> 15) << 31, exponent = (h >> 10) & 31u, mantissa = h & 1023u;
    uint32_t bits;
    if (exponent == 0u) {
        if (mantissa == 0u) bits = sign;
        else {
            float value = std::ldexp(float(mantissa), -24);
            return sign ? -value : value;
        }
    } else if (exponent == 31u) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
    }
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

struct D3d11CameraEffects {
    bool failed = false, ready = false;
    ID3D11VertexShader *vertex = nullptr;
    ID3D11PixelShader *effects = nullptr, *copy = nullptr, *add = nullptr;
    ID3D11Buffer *uniforms = nullptr, *weight = nullptr;
    ID3D11SamplerState *linear = nullptr, *point = nullptr;
    ID3D11BlendState *additive = nullptr;
    // A 1x1 view depth of "far, no character", for the shader's depth reads.
    ID3D11Texture2D *far_texture = nullptr;
    ID3D11ShaderResourceView *far_view = nullptr;
    // [0] the effects' picture, [1] a motion blur's average; at the main size.
    ID3D11Texture2D *pictures[2] = {};
    ID3D11RenderTargetView *picture_targets[2] = {};
    ID3D11ShaderResourceView *picture_views[2] = {};
    // The view depth and character mask the draws write (multisampled like
    // the main target), and its single-sample copy the effects read.
    ID3D11Texture2D *depth = nullptr, *depth_single = nullptr;
    ID3D11RenderTargetView *depth_target = nullptr;
    ID3D11ShaderResourceView *depth_view = nullptr;
    uint32_t depth_width = 0, depth_height = 0, depth_samples = 0;
    // The scene cleared the view depth this frame (so it was drawn).
    bool depth_drawn = false;
    // Wanted for the next frame: the effects are on.
    bool depth_wanted = false;
    // The subject and the sun, seen in this frame's draws.
    bool sun_seen = false;
    float sun_dir[3] = {}, sun_camera[16] = {};
    float scene_fov = 0.0f;
    float subject_offset = 0.0f, subject_depth = 0.0f;
    // Time of day's tint for what the game does not light (the baked stage
    // and the sky), taken at each present; a 3D draw since the present.
    bool tint_on = false, scene_drawn = false;
    float tint_rgb[3] = {1.0f, 1.0f, 1.0f};
    // A transparent screenshot's frame: only the characters, over nothing,
    // saved with its alpha and a depth picture, not shown.
    bool clear_shot = false;
    // A motion blur's sum, in half floats.
    ID3D11Texture2D *sum = nullptr;
    ID3D11RenderTargetView *sum_target = nullptr;
    ID3D11ShaderResourceView *sum_view = nullptr;
    uint32_t width = 0, height = 0;
    unsigned frame = 0;
    // The depth the effects read this frame (null: the far depth).
    ID3D11ShaderResourceView *depth_now = nullptr;

    // Taken at each present.
    RecompCameraEffects settings{};
    bool backdrop = false;
    float backdrop_rgb[3] = {};
    uint32_t blur_index = 0, blur_count = 0;
    bool blur_started = false;
    // The frame is a motion blur's picture before its last: not shown.
    bool hide = false;
    // What the window shows this frame: the guest's back buffer, or ours.
    ID3D11ShaderResourceView *back = nullptr, *shown = nullptr;

    // A video being recorded: ffmpeg through a pipe, a frame a present.
    FILE *pipe = nullptr;
    ID3D11Texture2D *staging = nullptr;
    uint32_t video_width = 0, video_height = 0;
    unsigned video_frames = 0;
    bool video_failed = false, video_warned = false;
    std::string video_path;
    // Its sound (video_sound.h).
    VideoSound sound;

    template <typename T> static void drop(T *&object)
    {
        if (object != nullptr) {
            object->Release();
            object = nullptr;
        }
    }

    void releaseTargets()
    {
        for (unsigned i = 0; i < 2; ++i) {
            drop(picture_views[i]);
            drop(picture_targets[i]);
            drop(pictures[i]);
        }
        drop(sum_view);
        drop(sum_target);
        drop(sum);
        width = height = 0;
    }

    void releaseDepth()
    {
        drop(depth_view);
        drop(depth_single);
        drop(depth_target);
        drop(depth);
        depth_width = depth_height = depth_samples = 0;
        depth_drawn = false;
    }

    void release()
    {
        stopVideo();
        releaseTargets();
        releaseDepth();
        drop(vertex);
        drop(effects);
        drop(copy);
        drop(add);
        drop(uniforms);
        drop(weight);
        drop(linear);
        drop(point);
        drop(additive);
        drop(far_view);
        drop(far_texture);
        ready = false;
    }

    static bool compile(const std::string &source, const char *entry, const char *target, ID3DBlob **blob)
    {
        ID3DBlob *errors = nullptr;
        const HRESULT result = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, entry, target,
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0u, blob, &errors);
        if (FAILED(result)) {
            std::fprintf(stderr, "recomp camera effects (d3d11): %s %s failed: %s\n", entry, target,
                errors != nullptr ? static_cast<const char *>(errors->GetBufferPointer()) : "no message");
        }
        drop(errors);
        return SUCCEEDED(result);
    }

    // Shaders, samplers and the far depth, once.
    bool setUp(ID3D11Device *device)
    {
        if (ready) return true;
        if (failed) return false;
        static const char plain[] =
            "Texture2D picture : register(t0);\n"
            "cbuffer Weight : register(b0) { float4 weight; };\n"
            "float4 vs(uint id : SV_VertexID) : SV_Position {\n"
            " float2 q = float2((id << 1) & 2, id & 2);\n"
            " return float4(q * 2.0 - 1.0, 0.0, 1.0); }\n"
            "float4 copy_ps(float4 p : SV_Position) : SV_Target { return picture.Load(int3(p.xy, 0)); }\n"
            "float4 add_ps(float4 p : SV_Position) : SV_Target { return picture.Load(int3(p.xy, 0)) * weight.x; }\n";
        const std::string effects_source = recomp_effects::effectsHlsl();
        ID3DBlob *vs = nullptr, *fx = nullptr, *cp = nullptr, *ad = nullptr;
        bool ok = compile(plain, "vs", "vs_4_0", &vs) && compile(effects_source, "ps_main", "ps_4_0", &fx) &&
            compile(plain, "copy_ps", "ps_4_0", &cp) && compile(plain, "add_ps", "ps_4_0", &ad);
        ok = ok && SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex));
        ok = ok && SUCCEEDED(device->CreatePixelShader(fx->GetBufferPointer(), fx->GetBufferSize(), nullptr, &effects));
        ok = ok && SUCCEEDED(device->CreatePixelShader(cp->GetBufferPointer(), cp->GetBufferSize(), nullptr, &copy));
        ok = ok && SUCCEEDED(device->CreatePixelShader(ad->GetBufferPointer(), ad->GetBufferSize(), nullptr, &add));
        drop(vs);
        drop(fx);
        drop(cp);
        drop(ad);
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(recomp_effects::EffectsUniforms);
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ok = ok && SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &uniforms));
        buffer.ByteWidth = 16u;
        ok = ok && SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &weight));
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        ok = ok && SUCCEEDED(device->CreateSamplerState(&sampler, &linear));
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        ok = ok && SUCCEEDED(device->CreateSamplerState(&sampler, &point));
        D3D11_BLEND_DESC blend{};
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = blend.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ok = ok && SUCCEEDED(device->CreateBlendState(&blend, &additive));
        // Half floats: 65504 far, 0 no character.
        const uint16_t far_texel[2] = {0x7BFFu, 0u};
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = texture.Height = 1u;
        texture.MipLevels = texture.ArraySize = 1u;
        texture.Format = DXGI_FORMAT_R16G16_FLOAT;
        texture.SampleDesc.Count = 1u;
        texture.Usage = D3D11_USAGE_IMMUTABLE;
        texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{far_texel, sizeof far_texel, 0u};
        ok = ok && SUCCEEDED(device->CreateTexture2D(&texture, &data, &far_texture));
        ok = ok && SUCCEEDED(device->CreateShaderResourceView(far_texture, nullptr, &far_view));
        if (!ok) {
            std::fprintf(stderr, "recomp camera effects (d3d11): setup failed; the frame is shown as drawn\n");
            release();
            failed = true;
            return false;
        }
        ready = true;
        std::fprintf(stderr, "recomp camera effects (d3d11): ready (colour, bloom, backdrops, motion blur, video)\n");
        return true;
    }

    static bool target(ID3D11Device *device, uint32_t w, uint32_t h, DXGI_FORMAT format, ID3D11Texture2D **texture,
        ID3D11RenderTargetView **view, ID3D11ShaderResourceView **resource)
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = desc.ArraySize = 1u;
        desc.Format = format;
        desc.SampleDesc.Count = 1u;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture)) &&
            SUCCEEDED(device->CreateRenderTargetView(*texture, nullptr, view)) &&
            SUCCEEDED(device->CreateShaderResourceView(*texture, nullptr, resource));
    }

    bool sized(ID3D11Device *device, uint32_t w, uint32_t h)
    {
        if (width == w && height == h && pictures[0] != nullptr) return true;
        releaseTargets();
        bool ok = target(device, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, &pictures[0], &picture_targets[0], &picture_views[0]) &&
            target(device, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, &pictures[1], &picture_targets[1], &picture_views[1]) &&
            target(device, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, &sum, &sum_target, &sum_view);
        if (!ok) {
            releaseTargets();
            return false;
        }
        width = w;
        height = h;
        blur_started = false;
        return true;
    }

    void pass(ID3D11DeviceContext *context, ID3D11RenderTargetView *output, ID3D11PixelShader *shader,
        ID3D11ShaderResourceView *picture, ID3D11Buffer *constants, ID3D11BlendState *blend) const
    {
        context->ClearState();
        const D3D11_VIEWPORT viewport = {0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f};
        context->RSSetViewports(1u, &viewport);
        context->OMSetRenderTargets(1u, &output, nullptr);
        if (blend != nullptr) context->OMSetBlendState(blend, nullptr, 0xFFFFFFFFu);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex, nullptr, 0u);
        context->PSSetShader(shader, nullptr, 0u);
        if (constants != nullptr) context->PSSetConstantBuffers(0u, 1u, &constants);
        ID3D11ShaderResourceView *views[2] = {picture, depth_now != nullptr ? depth_now : far_view};
        context->PSSetShaderResources(0u, 2u, views);
        ID3D11SamplerState *samplers[2] = {linear, point};
        context->PSSetSamplers(0u, 2u, samplers);
        context->Draw(3u, 0u);
        context->ClearState();
    }

    /* At each present, before the output pass: takes the free camera's
       effects, and works out what the window shows from the guest's back
       buffer (`back`, main size). */
    void present(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11ShaderResourceView *guest,
        uint32_t index, uint32_t count, uint32_t main_width, uint32_t main_height, uint32_t samples,
        float bigger = 1.0f)
    {
        back = guest;
        depth_now = nullptr;
        // The view depth drawn this frame, single-sampled for the effects.
        const bool depth_ready = depth_drawn && depth_target != nullptr && depth_view != nullptr;
        if (depth_ready) {
            if (depth_samples > 1u) context->ResolveSubresource(depth_single, 0u, depth, 0u, DXGI_FORMAT_R16G16_FLOAT);
            else context->CopyResource(depth_single, depth);
            depth_now = depth_view;
        }
        depth_drawn = false;
        const float subject = subject_depth;
        subject_depth = subject_offset = 0.0f;
        const bool sun = sun_seen;
        sun_seen = false;
        uint32_t w = 0, h = 0;
        if (guest != nullptr) {
            ID3D11Resource *resource = nullptr;
            guest->GetResource(&resource);
            ID3D11Texture2D *texture = nullptr;
            if (resource != nullptr) resource->QueryInterface(IID_PPV_ARGS(&texture));
            if (texture != nullptr) {
                D3D11_TEXTURE2D_DESC desc{};
                texture->GetDesc(&desc);
                w = desc.Width;
                h = desc.Height;
            }
            drop(texture);
            drop(resource);
        }
        recomp_camera_effects_take(&settings);
        recomp_camera_effects_report_subject(subject);
        tint_on = recomp_camera_effects_scene_tint(tint_rgb);
        scene_drawn = false;
        // The next frame's draws write the view depth while the effects are on.
        depth_wanted = g_d3d11_camera_depth && (settings.active || clear_shot);
        if (depth_wanted) wantDepth(device, main_width, main_height, samples);
        backdrop = settings.backdrop;
        std::memcpy(backdrop_rgb, settings.backdrop_rgb, sizeof backdrop_rgb);
        shown = back;
        hide = false;
        // A transparent screenshot's frame is not blurred.
        const bool blur = count > 1u && !clear_shot;
        if (back == nullptr || w == 0u || (!settings.active && !blur && !settings.recording && !clear_shot)) return;
        if (!setUp(device) || !sized(device, w, h)) return;
        if (settings.active || clear_shot) {
            RecompCameraEffects drawn = settings;
            // Without the view depth: no depth of field, and no mask.
            if (!depth_ready) drawn.depth_of_field = false;
            // The mask is the alpha only in a transparent screenshot's frame.
            drawn.transparent = clear_shot && depth_ready;
            if (drawn.focus == RECOMP_CAMERA_FOCUS_SUBJECT) drawn.focus = subject;
            recomp_effects::EffectsFrame frame;
            frame.width = float(w);
            frame.height = float(h);
            frame.bigger = bigger;
            frame.depth_drawn = depth_ready;
            frame.sun_seen = sun;
            frame.fov = scene_fov;
            std::memcpy(frame.sun_dir, sun_dir, sizeof frame.sun_dir);
            std::memcpy(frame.sun_camera, sun_camera, sizeof frame.sun_camera);
            frame.frame = ++this->frame;
            const recomp_effects::EffectsUniforms u = recomp_effects::effectsUniforms(drawn, frame);
            context->UpdateSubresource(uniforms, 0u, nullptr, &u, 0u, 0u);
            pass(context, picture_targets[0], effects, back, uniforms, nullptr);
            shown = picture_views[0];
        }
        if (blur) {
            // Pictures add up, weighted, into the sum; the last shows the average.
            if (index == 0u) {
                const float zero[4] = {};
                context->ClearRenderTargetView(sum_target, zero);
                blur_started = true;
            }
            if (!blur_started) return;
            const float w4[4] = {1.0f / float(count), 0.0f, 0.0f, 0.0f};
            context->UpdateSubresource(weight, 0u, nullptr, w4, 0u, 0u);
            pass(context, sum_target, add, shown, weight, additive);
            if (index + 1u < count) {
                hide = true;
                return;
            }
            blur_started = false;
            pass(context, picture_targets[1], copy, sum_view, nullptr, nullptr);
            shown = picture_views[1];
        }
    }

    // Our own picture this frame, or nullptr when the guest's is shown.
    ID3D11ShaderResourceView *ownPicture() const
    {
        return shown != back ? shown : nullptr;
    }

    /* What the window would show (main size), saved as a screenshot: a big
       screenshot's frame, which the window cannot take. */
    bool savePicture(ID3D11Device *device, ID3D11DeviceContext *context) const
    {
        if (shown == nullptr) return false;
        ID3D11Resource *resource = nullptr;
        shown->GetResource(&resource);
        ID3D11Texture2D *picture = nullptr;
        if (resource != nullptr) resource->QueryInterface(IID_PPV_ARGS(&picture));
        drop(resource);
        if (picture == nullptr) return false;
        D3D11_TEXTURE2D_DESC desc{};
        picture->GetDesc(&desc);
        desc.MipLevels = desc.ArraySize = 1u;
        desc.SampleDesc.Count = 1u;
        desc.SampleDesc.Quality = 0u;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0u;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0u;
        bool saved = false;
        ID3D11Texture2D *staging = nullptr;
        if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM && SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging))) {
            // The first level only: the guest's copy carries smaller ones.
            context->CopySubresourceRegion(staging, 0u, 0u, 0u, 0u, picture, 0u, nullptr);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(staging, 0u, D3D11_MAP_READ, 0u, &mapped))) {
                recomp_screenshot_save(static_cast<const uint8_t *>(mapped.pData), desc.Width, desc.Height, mapped.RowPitch);
                context->Unmap(staging, 0u);
                saved = true;
            }
        }
        drop(staging);
        drop(picture);
        return saved;
    }

    // Draws what the window shows into `output` (main size).
    void show(ID3D11DeviceContext *context, ID3D11RenderTargetView *output) const
    {
        pass(context, output, copy, shown, nullptr, nullptr);
    }

    /* A draw left out of a backdrop's frame: anything on the main target that
       is not a character's. */
    bool leaveOut(const RecompD3dPresenterDrawCommand &draw) const
    {
        return (backdrop || clear_shot) && !draw.target.offscreen && !draw.character;
    }

    // A backdrop's frame starts from its colour.
    void clearFor(RecompD3dPresenterClearCommand &clear) const
    {
        if ((!backdrop && !clear_shot) || clear.target.offscreen || (!clear.clear_color && !clear.clear_depth)) return;
        clear.clear_color = true;
        if (clear_shot) {
            clear.color = 0u; // nothing, alpha 0
            return;
        }
        const auto byte = [](float v) { return uint32_t(v <= 0.0f ? 0.0f : v >= 1.0f ? 255.0f : v * 255.0f + 0.5f); };
        clear.color = 0xFF000000u | (byte(backdrop_rgb[0]) << 16) | (byte(backdrop_rgb[1]) << 8) | byte(backdrop_rgb[2]);
    }

    // The view-depth target, at the main target's size and samples.
    void wantDepth(ID3D11Device *device, uint32_t w, uint32_t h, uint32_t samples)
    {
        if (depth != nullptr && depth_width == w && depth_height == h && depth_samples == samples) return;
        releaseDepth();
        if (w == 0u || h == 0u || samples == 0u) return;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = desc.ArraySize = 1u;
        desc.Format = DXGI_FORMAT_R16G16_FLOAT;
        desc.SampleDesc.Count = samples;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        bool ok = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &depth)) &&
            SUCCEEDED(device->CreateRenderTargetView(depth, nullptr, &depth_target));
        desc.SampleDesc.Count = 1u;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ok = ok && SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &depth_single)) &&
            SUCCEEDED(device->CreateShaderResourceView(depth_single, nullptr, &depth_view));
        if (!ok) {
            std::fprintf(stderr, "recomp camera effects (d3d11): view depth unavailable; no depth of field\n");
            releaseDepth();
            return;
        }
        depth_width = w;
        depth_height = h;
        depth_samples = samples;
    }

    // The view-depth target the main target's draws write too, or null.
    ID3D11RenderTargetView *depthTarget() const
    {
        return depth_wanted ? depth_target : nullptr;
    }

    // The scene's depth clear clears the view depth: far, no character.
    void clearDepth(ID3D11DeviceContext *context)
    {
        if (depthTarget() == nullptr) return;
        const float far_away[4] = {65504.0f, 0.0f, 0.0f, 0.0f};
        context->ClearRenderTargetView(depth_target, far_away);
        depth_drawn = true;
    }

    /* Time of day on one draw, as the SDL GPU presenter tints: unlit 3D draws,
       and 2D backgrounds before them (the sky), take the tint in the shader
       (`unlit`); lit scenery keeps its relit lights and tints only them
       (`lit`, applied to its light colours); characters only relight. */
    void sceneTint(const RecompD3dPresenterDrawCommand &draw, bool pretransformed, float unlit[3], float lit[3])
    {
        for (unsigned c = 0; c < 3u; ++c) unlit[c] = lit[c] = 1.0f;
        if (draw.target.offscreen) return;
        if (tint_on && !draw.character) {
            const bool background = pretransformed && !scene_drawn;
            if ((!pretransformed && !draw.directional.enabled) || background) std::memcpy(unlit, tint_rgb, sizeof tint_rgb);
            else if (draw.directional.enabled && !draw.program_count) std::memcpy(lit, tint_rgb, sizeof tint_rgb);
        }
        if (!pretransformed) scene_drawn = true;
    }

    // Each draw on the main target: the sun, and the subject among the characters.
    void observe(const RecompD3dPresenterDrawCommand &draw)
    {
        if (!draw.target.offscreen)
            recomp_effects::observeDraw(draw, sun_seen, sun_dir, sun_camera, subject_offset, subject_depth);
        recomp_effects::sceneFov(draw, scene_fov);
    }

    // Transparent screenshots can be drawn here: the view depth is.
    bool transparentReady() const
    {
        return settings.transparent && depth_view != nullptr && g_d3d11_camera_depth;
    }

    /* A transparent screenshot's frame: the picture with its alpha (colour
       divided by it: drawn over nothing, a partly covered pixel holds only
       its share) and the depth picture, white near and black far. */
    bool saveLayers(ID3D11Device *device, ID3D11DeviceContext *context)
    {
        if (shown == nullptr || depth_now == nullptr) return false;
        bool saved = false;
        ID3D11Resource *resource = nullptr;
        shown->GetResource(&resource);
        ID3D11Texture2D *picture = nullptr;
        if (resource != nullptr) resource->QueryInterface(IID_PPV_ARGS(&picture));
        drop(resource);
        if (picture == nullptr) return false;
        D3D11_TEXTURE2D_DESC desc{};
        picture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0u;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0u;
        ID3D11Texture2D *colour = nullptr, *depths = nullptr;
        D3D11_TEXTURE2D_DESC depth_desc{};
        depth_single->GetDesc(&depth_desc);
        depth_desc.Usage = D3D11_USAGE_STAGING;
        depth_desc.BindFlags = 0u;
        depth_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM && depth_desc.Width == desc.Width &&
            depth_desc.Height == desc.Height &&
            SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &colour)) &&
            SUCCEEDED(device->CreateTexture2D(&depth_desc, nullptr, &depths))) {
            context->CopyResource(colour, picture);
            context->CopyResource(depths, depth_single);
            D3D11_MAPPED_SUBRESOURCE c{}, d{};
            if (SUCCEEDED(context->Map(colour, 0u, D3D11_MAP_READ, 0u, &c))) {
                if (SUCCEEDED(context->Map(depths, 0u, D3D11_MAP_READ, 0u, &d))) {
                    const uint32_t w = desc.Width, h = desc.Height;
                    std::string straight(size_t(w) * h * 4u, '\0'), grey(size_t(w) * h * 4u, '\0');
                    const float scale = 1.0f / std::log2(101.0f);
                    for (uint32_t y = 0; y < h; ++y) {
                        const auto *from = static_cast<const uint8_t *>(c.pData) + size_t(y) * c.RowPitch;
                        const auto *depth_row = reinterpret_cast<const uint16_t *>(
                            static_cast<const uint8_t *>(d.pData) + size_t(y) * d.RowPitch);
                        for (uint32_t x = 0; x < w; ++x) {
                            uint8_t *to = reinterpret_cast<uint8_t *>(&straight[(size_t(y) * w + x) * 4u]);
                            const unsigned a = from[x * 4u + 3u];
                            for (unsigned k = 0; k < 4u; ++k) to[k] = from[x * 4u + k];
                            if (a != 0u && a != 255u)
                                for (unsigned k = 0; k < 3u; ++k) {
                                    const unsigned v = (from[x * 4u + k] * 255u + a / 2u) / a;
                                    to[k] = uint8_t(v > 255u ? 255u : v);
                                }
                            const float view = std::fmax(0.0f, halfValue(depth_row[x * 2u]));
                            const float closeness = 1.0f - std::fmin(1.0f, std::log2(1.0f + view) * scale);
                            uint8_t *g = reinterpret_cast<uint8_t *>(&grey[(size_t(y) * w + x) * 4u]);
                            g[0] = g[1] = g[2] = uint8_t(closeness * 255.0f + 0.5f);
                            g[3] = 255u;
                        }
                    }
                    recomp_screenshot_save_layers(reinterpret_cast<const uint8_t *>(straight.data()), w, h, w * 4u,
                        reinterpret_cast<const uint8_t *>(grey.data()));
                    saved = true;
                    context->Unmap(depths, 0u);
                }
                context->Unmap(colour, 0u);
            }
        }
        drop(colour);
        drop(depths);
        drop(picture);
        return saved;
    }

    // ---- Video: every frame shown while the path plays with Record on.

    static std::string ffmpeg()
    {
        if (const char *chosen = std::getenv("RECOMP_FFMPEG"))
            if (chosen[0] != '\0') return chosen;
        return "ffmpeg"; // from PATH
    }

    void stopVideo()
    {
        if (pipe != nullptr) {
            const int status = _pclose(pipe);
            const bool saved = status == 0 && !video_failed;
            const bool with_sound = sound.finish(ffmpeg(), video_path, saved);
            std::fprintf(stderr, saved ? "recomp video: saved %s (%u frames%s)\n"
                                       : "recomp video: %s failed after %u frames%s\n",
                video_path.c_str(), video_frames, with_sound ? ", with sound" : "");
        } else {
            sound.finishWithout();
        }
        pipe = nullptr;
        drop(staging);
        video_frames = 0;
        video_failed = false;
    }

    bool startVideo(ID3D11Device *device, ID3D11Texture2D *picture)
    {
        D3D11_TEXTURE2D_DESC desc{};
        picture->GetDesc(&desc);
        if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || desc.SampleDesc.Count != 1u) {
            std::fprintf(stderr, "recomp video: the frame is not 8-bit BGRA here; not recording\n");
            return false;
        }
        char path[1200];
        if (!recomp_screenshot_path(path, sizeof path)) return false;
        const std::string stem(path, std::strlen(path) - 4u);
        video_path = stem + ".mp4";
        for (unsigned n = 2u; GetFileAttributesA(video_path.c_str()) != INVALID_FILE_ATTRIBUTES && n < 100u; ++n)
            video_path = stem + "-v" + std::to_string(n) + ".mp4";
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0u;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0u;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) return false;
        video_width = desc.Width;
        video_height = desc.Height;
        // cmd.exe's own quoting: the whole command in quotes, each path in its own.
        const std::string command = "\"\"" + ffmpeg() + "\" -n -loglevel error -f rawvideo -pix_fmt bgra -s " +
            std::to_string(video_width) + "x" + std::to_string(video_height) +
            " -r 60 -i - -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p \"" + video_path + "\"\"";
        pipe = _popen(command.c_str(), "wb");
        if (pipe == nullptr) {
            drop(staging);
            return false;
        }
        sound.start(video_path);
        std::fprintf(stderr, "recomp video: recording %ux%u to %s\n", video_width, video_height, video_path.c_str());
        return true;
    }

    /* After the output pass, while the path plays with Record on: the frame
       shown goes to ffmpeg. The read waits for the GPU; recording is slower
       here than on the SDL GPU presenter, but the video keeps every frame. */
    void record(ID3D11Device *device, ID3D11DeviceContext *context)
    {
        const bool wanted = settings.recording && shown != nullptr && !hide;
        if (!settings.recording) {
            stopVideo();
            video_warned = false;
            return;
        }
        if (!wanted) return;
        ID3D11Resource *resource = nullptr;
        shown->GetResource(&resource);
        ID3D11Texture2D *picture = nullptr;
        if (resource != nullptr) resource->QueryInterface(IID_PPV_ARGS(&picture));
        drop(resource);
        if (picture == nullptr) return;
        D3D11_TEXTURE2D_DESC desc{};
        picture->GetDesc(&desc);
        if (pipe != nullptr && (desc.Width != video_width || desc.Height != video_height)) stopVideo();
        if (pipe == nullptr && !video_warned && !startVideo(device, picture)) video_warned = true;
        if (pipe != nullptr) {
            context->CopyResource(staging, picture);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(staging, 0u, D3D11_MAP_READ, 0u, &mapped))) {
                const auto *rows = static_cast<const uint8_t *>(mapped.pData);
                for (uint32_t y = 0; y < video_height && !video_failed; ++y)
                    if (std::fwrite(rows + size_t(y) * mapped.RowPitch, 4u, video_width, pipe) != video_width)
                        video_failed = true;
                context->Unmap(staging, 0u);
                ++video_frames;
                sound.frame();
            }
            if (video_failed) {
                // ffmpeg quit (no encoder, a full disk): not again until asked again.
                stopVideo();
                video_warned = true;
            }
        }
        drop(picture);
    }
};

#endif
