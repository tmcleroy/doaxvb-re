# Time of day

While the free camera flies, the **Time of day** row of the camera settings
(Tab, or Back on a controller) sets the hour from 06:00 to 22:00 in half
hours; the first value leaves the stage's own. The game bakes one time into
each stage, so this changes three things live, on the SDL GPU presenter
(macOS and Linux):

1. **Lights.** The scene's four lights (docs/stage-lights.md) leave the
   readable `0x0017CB50` relit for the hour: every colour changes by the
   chosen time's over the stage's time, so each place and each of the rigs a
   match switches between keeps its own character. Light 0's ambient colour
   (the scene's ambient) changes the same way. The key light keeps its
   compass heading and takes the hour's height, but stays at least 25
   degrees up, or flat lit ground would go dark at sunset.
2. **Sun.** The sun the game projects its own shadows along, and the sun
   shadows follow (`0x00333870`), takes the hour's true height on the key
   light's heading; it is put back when the time returns to the stage's own.
3. **Baked scene.** What the game does not light (the stage's baked colours,
   the sky and other 2D backgrounds drawn before the first 3D draw after the
   scene's depth clear) is multiplied by the hour's tint over the stage's.
   Lit scenery larger than a character's limb, such as the sea, tints only
   its glow (ambient and emissive); characters, skinned joints and lit pieces
   no larger than a limb, are only relit. The game's 2D overlay stays as it is.

The stage's own time comes from the loaded court's stage number
(`recomp_viewer_stage_time`, from the stage sweep in model-viewer.md); a
stage not in it counts as day. Looks for hours between the keyframes are
blended (`recomp-runtime/time_of_day.c`):

| Hour | Keyframe |
|---|---|
| 06:30 | morning, made from day and sunset |
| 12:00 | the game's day |
| 16:30 | golden hour, made from day and sunset |
| 18:00 | the game's sunset |
| 19:00 | the game's dusk |
| 21:00 | night, made from dusk |

Day, sunset and dusk are the game's own key lights on the first beach's
three courts (stages 4, 5 and 39), read with `RECOMP_STAGE_LIGHTS_DUMP=1`,
which logs each light record when it changes and the sun direction. Pinning
a court with `RECOMP_TITLE_IDLE=cpu-exhibition` and
`RECOMP_CPU_EXHIBITION_COURT=0..8` (three places at dusk, day and sunset)
showed that a match switches between several rigs as its shots change, so
the stage's time cannot be read off its lights.

Checked on macOS only, by CPU Exhibition matches on the first beach by day
set to 18:00 and 21:00: the girls, sand, sky and sea follow the hour and the
sun shadows lengthen toward sunset. Not done: the D3D11 presenter, hotel
and shop interiors (they count as day), and the sea's reflections of the sky.
