# Graphics options

Seven launcher options improve how the game looks without changing what it
draws. On macOS and Linux they are in the launcher's **Graphics** column; on
Windows they have their own **Enhancements** page. Each is an environment
variable the launcher sets, so a script can set it too.

| Option | Variable | Launcher value | Default |
|---|---|---|---|
| Soft sun shadows | `RECOMP_D3D_SUN_SOFT` | `1` (0 to 1) | Off |
| Anisotropic filtering | `RECOMP_D3D_ANISOTROPY` | `1` on, `0` off | On |
| HDR output (macOS, Linux) | `RECOMP_D3D_HDR` | `1` on, `0` off | Off |
| Ambient occlusion | `RECOMP_D3D_OCCLUSION` | `0.5` (0 to 1) | Off |
| Bloom and sun glare | `RECOMP_D3D_BLOOM` | `0.3` (0 to 1) | Off |
| Soft skin | `RECOMP_D3D_SKIN_SOFT` | `1` (0 to 1) | Off |
| Skin sheen | `RECOMP_D3D_SKIN_SHEEN` | `1` (0 to 1) | Off |

`tools/run_game.py` (macOS, Linux) and `tools/launcher.ps1` (Windows) turn the
saved choices in `private/launcher.json` (`sun_soft`, `anisotropy`, `hdr`,
`ambient_occlusion`, `bloom`, `skin_soft`, `skin_sheen`) into these values.

## HDR output

On an HDR display the window's swapchain switches to HDR: extended linear
where the display takes it (Metal's EDR on macOS), else HDR10. The game's
picture stays as it is up to 60% of SDR white (linear), shown at the
display's SDR white, and anything brighter eases up to the peak along a
cubic, all three channels together so colours hold: the sun, sun glare,
sea glints and skin sheen use the headroom. The peak is 4x SDR white by
default (`RECOMP_D3D_HDR_PEAK`, 1 to 16), capped at the headroom macOS
reports for the window's display. HDR10 converts to BT.2020 and encodes PQ
with SDR white at 203 nits. On a display without HDR, or if the pass
cannot be set up (the log says so), the window stays SDR.

Only the window changes: screenshots and videos are taken before this
step, so they stay SDR. The runtime's own panel is drawn into the HDR
picture with its SDR colours, so it looks a little darker. Direct3D 11
(Windows) has no HDR output yet. On macOS the window took extended linear
and the pass ran without errors; the Vulkan shader compiles with glslang.
How it looks on an HDR screen has not been checked yet: the display was
asleep when this was written.

## Soft sun shadows

With **Sun shadows** on ([`sun-shadow.md`](sun-shadow.md)), this gives the
sun a size, so a shadow is sharp where it leaves its caster (feet on the
sand) and softens with distance from it (the far end of a post's or a
palm's shadow), as real sun shadows do. Each shadowed pixel first reads the
map's raw depths in a 4x4 search 16 texels wide; if nothing there is nearer
the sun it is lit, otherwise the blockers' average depth gives the penumbra
a sun of that size casts over the distance between them, and the existing
5x5 comparison filter widens to it (up to 8 texels a step). At 1 the
penumbra grows 0.03 units per unit of distance, about three times the real
sun's, which shows without blurring nearby shadows (`sunSoftness` in
`d3d_sun_shadow_model.h`, passed to the shaders as `sun_light.w`).

Metal and Vulkan bind the shadow map a second time, with a nearest sampler
and no comparison, for the search (slot 3); Direct3D 11 reads it with
`Load`. On frozen attract scenes on macOS, the far ends of the posts' and
palms' shadows soften and the shadows at the girls' feet stay sharp; no
other change showed. The Vulkan shaders compile with glslang; the
Direct3D 11 code compiles with llvm-mingw and its HLSL with glslang's HLSL
front end, and has not run on Windows yet.

## Anisotropic filtering

Mipmapped scene textures seen at a grazing angle (sand, floors, the court)
are sampled with 16x anisotropic filtering, so they stay sharp into the
distance instead of blurring. Direct3D 11 always did this for the draws it
picks (`lookupDrawSampler`: mipmapped, not frames, render targets, palettes or
reflections); the Metal and Vulkan presenter now picks the same draws. Off
goes back to trilinear filtering on both.

## Ambient occlusion, bloom and sun glare

These are the free camera's effects ([`free-camera.md`](free-camera.md)),
run in every scene. `recomp_camera_effects_take` (`camera_effects.c`) adds
the launcher's amounts to what the free camera publishes: when the camera
is not flying, only occlusion and bloom show and its own look (filter,
exposure, depth of field and the rest) stays off; while it flies, each is
the stronger of the launcher's and its own row's. The presenters build the
effects pass and the view depth when either the free camera or one of these
is on.

Occlusion reaches a quarter of a world unit, which needs the scene camera's
field of view. Without the free camera nothing publishes one, so each
presenter works it out from the frame's 3D draws (`sceneFov`,
`camera_effects_shader.h`): the view only turns and moves, so the length of
the view-projection's y column over its w column is the projection's
cot(fov/2). A run showed 0.62 radians on the usual match camera and 0.17 to
0.43 on the game's zoomed shots.

## Soft skin and skin sheen

Skin shading works on the girls' lit, opaque, textured draws on the main
target: draws marked as a character's (`recomp_actor_pose_character_world`,
the same test the free camera uses; with either skin option on it is wired
up without the free camera too), with directional lighting, no vertex
program, no blending (hair is blended and stays out) and no reflection.

In those draws the pixel shader decides per pixel how much the texel looks
like skin (`skin_mask`): warm (red above green, green at or above blue),
moderately saturated and not dark. Swimsuits in strong colours, white, black
and blue stay out; a beige or tan suit can take a little of it. Where it is
skin, the draw's lights are evaluated per pixel:

- **Soft skin** keeps the lit side exactly as the game lights it and gives
  the shadow's edge extra light, tinted red, out to a third of the way past
  it: what passes under the skin and comes out on the dark side. Lighting
  per pixel also brings out the body's shape that the per-vertex lighting
  flattened.
- **Skin sheen** adds a soft highlight of the lights' own colour: two
  Blinn-Phong lobes (a tight one and a broad one) with a skin's Fresnel
  (0.028 head-on), toward the camera as a non-local viewer. It shows mostly
  as a rim along edges that face the light.

On Metal and Vulkan these are a shader variant (`skin` in the pipeline key,
`skinSource` in `d3d_presenter_sdl_gpu.cpp`) that passes the lights' normal
to the pixel shader. Direct3D 11 builds one shader per vertex format, so with
skin on every lit shader carries the normal and world position down and a
per-draw constant (`skin.w`) chooses (`skinShaderSource` in
`d3d_presenter_d3d11.cpp`; point lights are evaluated as its per-pixel
lighting does). The direction toward the camera comes from the draw's
view-projection (minus its w column).

## Verification

On macOS (Metal), runs froze the attract scene and turned skin shading off
and on every second, so the same frame could be compared. The values were
tuned from those pairs: the first wrap lit Ayane's whole back, which faced
away from the sun, and a broad highlight washed Kasumi's front in the
evening sun's colour; both were narrowed. With the free camera off, skin
draws ran (about 300,000 in 55 seconds), and a run with sun shadows, skin,
occlusion and bloom all on compiled every shader. The Vulkan skin shaders
compile with glslang. The Direct3D 11 code compiles with llvm-mingw, with
and without RTGI, and the HLSL it adds compiles with glslang's HLSL front
end; it has not run on Windows yet. If its pixel shader lacks the lines skin
shading anchors on, it logs `skin shading unavailable` and the draw is left
as it was.
