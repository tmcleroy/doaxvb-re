# Scene lights

The game lights its characters and props with four lights per scene, the
stage's rig, plus an ambient colour. This page describes the readable code
that turns the rig into D3D lights, the first step toward changing the time
of day while the camera flies.

## How the game sets them

Every scene's frame code copies its rig, four game light records of 0x58
bytes at `0x009D4F58`, to `0x009D5240`, then:

1. calls `0x0017CB50` once a light, with EDI the record and the light's slot
   (0 to 3) on the stack. It fills slot n of the D3D light table at
   `0x00A243F0 + n * 0x70`;
2. calls `0x0017CE00` with light 0's ambient colour (three floats), which
   packs the ambient render state into `0x00A243E4`;
3. calls `0x0017CC50`, which hands the table to the device (`SetLight`
   `0x001E4120`, `LightEnable` `0x001E4300`) with the ambient colour.

A game light record (`RecompStageLight` in `recomp-runtime/stage_lights.h`):

| Offset | Field |
|---|---|
| +0x00 | kind: 0 off, 1 directional, 2 point, 3 spot (D3D types from the table at `0x002870E0`) |
| +0x04, +0x08 | ambient alpha, ambient colour |
| +0x14, +0x18 | diffuse alpha, diffuse colour |
| +0x24, +0x28 | yaw and pitch of the direction the light comes from, binary angles (65536 a turn; `0x00281288` is 2π/65536) |
| +0x2C | position |
| +0x38 | attenuation 0 to 2 |
| +0x44, +0x48 | range, falloff |
| +0x4C, +0x50 | spot cone theta, phi |
| +0x54 | specular scale |

The D3D light (`D3DLIGHT8`, then two more words) takes the diffuse colour and
alpha, a specular colour of the diffuse colour times the scale (multiplied at
double precision, as the x87 does) with the diffuse alpha, the ambient colour
and alpha, the position, the opposite of the direction `0x0017B150` gives for
(-yaw, pitch), range, falloff, attenuation and cone, then the scale and
whether the light is on. The ambient render state is opaque, with each
channel 255 at 1 or more, 0 at 0 or less, and otherwise its value times 255
truncated (`_ftol`).

## Readable code and seams

`recomp-runtime/stage_lights.c` holds the record layout and both
conversions; `stage_lights_adapter.c` registers them for `0x0017CB50` and
`0x0017CE00`, which the recipe lists as manual call targets (regeneration
changes only their 544 call sites). `0x0017B150`, the direction from two
angles, and the sine and cosine it calls (`0x0017AB70`, a library polynomial)
stay generated; the adapter calls `0x0017B150` as the game does.

## Verification

`RECOMP_STAGE_LIGHTS_VERIFY=1` runs the generated function beside the
readable one on every call, puts memory back between them, and compares the
bytes each wrote and the registers each left. Over the attract demo's courts
on macOS (about two minutes) that was 222,600 light conversions and 55,200
ambient colours with no difference. `stage_lights_test.c` covers the layout,
the conversion and the packing without the game.
