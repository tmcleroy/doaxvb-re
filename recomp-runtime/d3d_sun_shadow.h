#ifndef DOAXBV_D3D_SUN_SHADOW_H
#define DOAXBV_D3D_SUN_SHADOW_H

#include "d3d_sun_shadow_model.h"

// Internal D3D11 experiment; all GPU objects are created only on explicit opt-in.
namespace {

struct SunCasterLayout {
    uint32_t fvf;
    unsigned weights;
    ID3D11InputLayout *input;
    ID3D11VertexShader *shader;
};

struct SunShadow : SunCamera {
    bool enabled = false, valid = false, casting = false;
    unsigned casters = 0, game_shadows = 0;
    unsigned prop_shadows = 0;
    // Game shadow opacity (texture-factor alpha) seen last frame; 0 when the game drew none.
    float game_alpha = 0, seen_alpha = 0;
    // Caster pass marks rigid models (ball, props) in draw order; the visible pass
    // replays the same packet, so draw_index finds the flag without rescanning vertices.
    std::vector<uint8_t> compact;
    size_t draw_index = 0;
    ID3D11Texture2D *texture = nullptr;
    ID3D11DepthStencilView *depth = nullptr;
    ID3D11ShaderResourceView *view = nullptr;
    ID3D11Buffer *constants = nullptr;
    ID3D11SamplerState *sampler = nullptr;
    ID3D11RasterizerState *rasterizer = nullptr;
    // Depth-only caster pipeline, built on first use; pass state binds once per pass.
    ID3D11Buffer *caster_constants = nullptr;
    ID3D11PixelShader *caster_alpha = nullptr;
    std::vector<SunCasterLayout> caster_layouts;
    bool caster_failed = false, caster_bound = false;
};

void sunRelease(SunShadow &sun)
{
    for (auto &caster : sun.caster_layouts) {
        if (caster.input) caster.input->Release();
        if (caster.shader) caster.shader->Release();
    }
    if (sun.caster_alpha) sun.caster_alpha->Release();
    if (sun.caster_constants) sun.caster_constants->Release();
    if (sun.rasterizer) sun.rasterizer->Release();
    if (sun.sampler) sun.sampler->Release();
    if (sun.constants) sun.constants->Release();
    if (sun.view) sun.view->Release();
    if (sun.depth) sun.depth->Release();
    if (sun.texture) sun.texture->Release();
    sun = {};
}

bool sunCreate(SunShadow &sun, ID3D11Device *device)
{
    if (!sun.enabled) return false;
    if (sun.texture) return true;
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = texture.Height = kSunMapSize;
    texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
    texture.Format = DXGI_FORMAT_R32_TYPELESS;
    texture.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    D3D11_DEPTH_STENCIL_VIEW_DESC depth{};
    depth.Format = DXGI_FORMAT_D32_FLOAT;
    depth.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_R32_FLOAT;
    view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels = 1;
    D3D11_BUFFER_DESC constants{};
    constants.ByteWidth = 40*sizeof(float);
    constants.Usage = D3D11_USAGE_DYNAMIC;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    for (float &v : sampler.BorderColor) v = 1;
    sampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    // Clamp instead of clip: fronds nearer the sun than the box still occlude.
    rasterizer.DepthClipEnable = FALSE;
    rasterizer.DepthBias = 100;
    rasterizer.SlopeScaledDepthBias = 2;
    if (FAILED(device->CreateTexture2D(&texture, nullptr, &sun.texture)) ||
        FAILED(device->CreateDepthStencilView(sun.texture, &depth, &sun.depth)) ||
        FAILED(device->CreateShaderResourceView(sun.texture, &view, &sun.view)) ||
        FAILED(device->CreateBuffer(&constants, nullptr, &sun.constants)) ||
        FAILED(device->CreateSamplerState(&sampler, &sun.sampler)) ||
        FAILED(device->CreateRasterizerState(&rasterizer, &sun.rasterizer))) {
        sunRelease(sun);
        std::fprintf(stderr, "recomp sun shadow: resource creation failed; disabled\n");
        return false;
    }
    std::fprintf(stderr, "recomp sun shadow: %ux%u D32 comparison map\n", kSunMapSize, kSunMapSize);
    return true;
}

void sunShaderSource(std::string &source)
{
    source.insert(0, std::string(R"(
cbuffer SunShadow : register(b1) {
    row_major float4x4 sun_projection;
    float4 sun_flags;
    row_major float4x4 sun_object; // world matrix; unlit receivers have no lighting position
    float4 sun_light; // xyz: unit vector toward the sun; w: soft shadows' sun size (sunSoftness)
};
Texture2D<float> sun_depth : register(t2);
SamplerComparisonState sun_sampler : register(s2);
static const float sun_texel = 1.0/)") + std::to_string(kSunMapSize) + R"(;
static const float sun_size = )" + std::to_string(kSunMapSize) + R"(;
float sunVisibility(float3 p, float soft, bool plane) {
    float3 q = mul(float4(p,1),sun_projection).xyz;
    float2 uv = q.xy * float2(0.5,-0.5) + 0.5;
    float2 dx = ddx(uv), dy = ddy(uv);
    float2 dz = float2(ddx(q.z), ddy(q.z));
    float2 footprint = abs(dx) + abs(dy);
    if (any(abs(q.xy) >= 1) || q.z <= 0 || q.z >= 1) return 1;
    // One kernel at every distance: taps stay within two texels of each other so a
    // thin distant line cannot fall between them, and near shadows stay sharp.
    float2 spacing = clamp(footprint/5, soft*sun_texel, max(soft,2)*sun_texel);
    float determinant = dx.x*dy.y-dx.y*dy.x;
    // Receiver-plane depth only suits flat ground; on curved models it swings near silhouettes.
    float2 slope = plane && abs(determinant) > 1e-12 ?
        float2(dy.y*dz.x-dx.y*dz.y, dx.x*dz.y-dy.x*dz.x)/determinant : 0;
    // Each tap blends neighbouring texels, so sloped ground needs one texel of slope as bias.
    float bias = 0.0002 + dot(abs(slope), sun_texel);
    // Soft sun shadows: the blockers' average depth in a 4x4 search, then a
    // filter as wide as the penumbra a sun of that size casts from there.
    if (sun_light.w > 0) {
        float found = 0, total = 0;
        [unroll] for (int sy=0; sy<4; ++sy)
            [unroll] for (int sx=0; sx<4; ++sx) {
                float2 at = saturate(uv + (float2(sx,sy)-1.5)*(16.0/1.5)*sun_texel);
                float d = sun_depth.Load(int3(at*(sun_size-1), 0));
                if (d < q.z-bias) { found += 1; total += d; }
            }
        if (found == 0) return 1;
        float penumbra = (q.z - total/found) * sun_light.w;
        spacing = max(spacing, min(penumbra*0.25, 8*sun_texel));
    }
    float visibility = 0;
    [unroll] for (int y=0; y<5; ++y)
        [unroll] for (int x=0; x<5; ++x) {
            float2 offset = (float2(x,y)-2)*spacing;
            visibility += sun_depth.SampleCmpLevelZero(sun_sampler,uv+offset,q.z+dot(slope,offset)-bias);
        }
    visibility /= 25;
    // Fade over the outer tenth of the box so shadows do not cut off at its edge.
    return lerp(1, visibility, saturate((1-max(abs(q.x),abs(q.y)))*10));
}
)");
    source.insert(source.find("struct VSOut {")+std::strlen("struct VSOut {"),
        "\n    float3 sun_world : TEXCOORD8;\n    float3 sun_normal : TEXCOORD14;\n");
    source.insert(source.find("    VSOut output;")+std::strlen("    VSOut output;"),
        "\n    output.sun_world=mul(float4(input.position.xyz,1),sun_object).xyz;\n    output.sun_normal=0;\n");
    const char *anchor = "        if (directional_flags.y > 0.5f && dot(n,n)>0) n *= rsqrt(dot(n,n));\n";
    auto lit = source.find(anchor);
    if (lit != std::string::npos) lit += std::strlen(anchor);
    if (lit != std::string::npos)
        source.insert(lit, "        output.sun_world=world_pos; output.sun_normal=n;\n");
    // Mip averages erase thin net wires before the alpha cutoff.
    source.insert(source.find("    if (draw_flags.y > 0.5f) {"),
        "    if (sun_flags.z > 0 && draw_flags.x > 0.5f)\n"
        "        shaded.a = guest_texture.SampleLevel(guest_sampler, input.texcoord * texture_flags.xy, 0).a;\n");
    source.insert(source.find("    float4 shaded = input.color;"),
        "    if (sun_flags.x > 0) {\n"
        "        float3 sn = input.sun_normal;\n"
        "        sn *= dot(sn,sn) > 0 ? rsqrt(dot(sn,sn)) : 0;\n"
        "        float ndl = dot(sn, sun_light.xyz);\n"
        // Per-pixel normal offset: ~2.5 texels facing the sun, ~4.5 at grazing angles.
        "        float visibility = sun_flags.w > 0 ?\n"
        // Models: look up from half a unit toward the sun, so their own body (under half a
        // unit thick) never shadows them while palms and posts still do, and skip any normal
        // term because limb normals break at the joints. A ~40 cm filter keeps frond shade soft.
        "            sunVisibility(input.sun_world + sun_light.xyz*0.5, 6, false) :\n"
        "            sunVisibility(input.sun_world + sn*(0.04+0.03*(1-saturate(ndl))), 0.6, true);\n"
        "        input.color.rgb *= 1 - sun_flags.y*(1-visibility);\n"
        "    }\n");
}

} // namespace
#endif
