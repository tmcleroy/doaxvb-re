# DOA3 <-> DOAXBV whole-character port: methodology (paused)

Status as of 2026-10-16: paused in favour of a smaller goal (DOAXBV swimsuits
on DOA3's own fighters, keeping DOA3's heads, hair and physics). This page
records how the whole-character version works, what was learned, and what
was left open, so it can be resumed.

Nothing here contains game data. Every asset is built locally from a
user-owned disc and stays under `private/`.

## Where the code lives

| Repo | Branch | Contents |
|---|---|---|
| doaxvb-re | `crossport-fullbody` | `recomp-runtime/mesh_dump.{c,h}`, `costume_inject.{c,h}` (DOA3 characters in DOAXBV), `tools/mesh_dump_export.py`, `tools/crossport/` (converters, checkers, run scripts), this page |
| doa3-re | `crossport-fullbody` | the same `mesh_dump` and a `costume_inject` for DOAXBV girls in DOA3, launcher costume pickers |

The doa3-re branch is based on the macOS/Linux port (1827bd0); the doaxvb-re
branch on upstream main e282a3f. The `tools/crossport` scripts assume the
local layout `~/code/recomp/{doaxvb-re,doa3-re}` with worktrees at
`private/wt-meshdump`, a Python venv with numpy and Pillow, and dumps under
`private/`. Adjust the paths at the top of each script to resume elsewhere.

## Pipeline

1. **Dump.** The runtime's mesh dump (`RECOMP_MESH_DUMP=<dir>` with
   `RECOMP_MESH_DUMP_AT=<presents>` or the F9 key for a burst of 8 frames)
   writes, per present, every draw's vertex and index bytes, world/view/
   projection matrices, texture and palette bytes, material, blend and alpha
   state, and with `RECOMP_MESH_DUMP_RAM=1` a full RAM snapshot (needed for
   the bone banks).
2. **Identify.** `mesh_dump_export.py` matches each draw's vertex bytes to an
   AFS archive entry (cached index in `afs-index/`). Draws with no match are
   CPU-written (DOA3 joints and hair, DOAXBV elbows and hair).
3. **Bind.** Each draw's WORLD0 equals one bone matrix of the character's bank
   (exactly for rigid parts), which assigns parts to bones and gives the
   bone-local rest shape: `O = W @ inverse(B[bone])`.
4. **Build an asset** (`build_costume_asset.py` for DOAXBV girls -> DXC1;
   `build_doa3_asset.py` for DOA3 fighters -> D3C1). Format v6 holds bone
   relatives and hierarchy offsets, textures, and per part: binds, material,
   alpha test, optional soft-part spring data (gravity rest rotation, hair
   anchor, per-vertex jiggle mask).
5. **Inject.** In the target game the injector recognises the fighter's draws
   by bone-bank match, suppresses them, and on the first matched draw of each
   frame skins and submits the asset with the target's lighting template.
   F5/F6 cycle a pool (`RECOMP_COSTUME_DIR`).

## Skeleton facts

- Bone banks: DOAXBV `0x4D3650 + actor*0x800` (32 bones); DOA3 fighters
  `0x4785D0`, `0x478B90`, tag partners `0x479150`, `0x479710` (23 bones,
  stride 0x5C0). Row-vector 4x4, 64 bytes per bone.
- Shared numbering: 0 chest, 1 head, 2 hip, 3/9 feet, 4/10 forearms, 5/11
  hands, 6/12 thighs, 7/13 shins, 8/14 upper arms. DOAXBV-only: 15 root, 16/20
  clavicles, 17/21 eyes, 18/22 toes, 19 neck, 23 waist, 24-31 derived helpers.
  DOA3-only: 15/16 eyes, 19/20 forearm copies; 17/18/21/22 unidentified
  (candidates for DOA3's chest physics).
- Limb axes differ by fixed rotations (`correction` tables in both injectors).
  Torso, head, hands and feet share axes.
- Retargeting keeps the target's rotations and rebuilds positions from the
  source's bone offsets, so joint skins built for the source proportions
  close. DOAXBV-only bones ride their source-frame transform relative to a
  shared parent. DOA3 has no neck: the DOAXBV neck is blended halfway between
  the chest-carried and head-carried transforms (`RECOMP_COSTUME_NECK`).

## Lessons (each cost real time)

- **Capture broadly.** DOAXBV culls off-screen parts per frame; an asset from
  one camera can miss feet or a shin. Build from many frames and audit that
  every rigid bone has a part (see `validate_asset.py`).
- **DOA3 CPU skins share one buffer.** A joint draw's vertex range also holds
  every earlier piece in that buffer. Trim each draw to the vertices its
  indices use before rigging, deduplicating, or keying motion tracks.
- **Dense frames drift across fighters.** A dump spanning a round change holds
  the next fighter in the same bank. `same_fighter()` keeps frames whose
  texture contents match the majority.
- **Fit joint weights to observed motion.** For each CPU joint vertex, fit
  blend weights that reproduce DOA3's own vertex positions over many frames.
  Bound two-bone weights to [0, 1]; keep the fit only where it explains the
  motion (residual < 2 cm). A strong ridge toward a distance prior swamps the
  data (positions are metres, motion centimetres).
- **Choose bones by motion, not distance**, but only among bones near the
  vertex: in short captures a distant bone can match by coincidence.
- **Split pieces that span limbs** (the torso skin touches both arms and both
  legs) into triangle groups of at most four bones.
- **Hand weight to the child bone near a joint**; the neck otherwise follows
  the chest alone.
- **Hair rest shape from a calm window.** Fight frames catch hair mid-swing;
  their average is a blur and any single frame may be swung. Use a short
  calm window (frames a few presents apart) for the rest shape and mean head
  rotation.
- **DOA3 draws hair twice** (translucent without depth, then with depth).
  Keep only the solid pass when both exist.
- **Snap joint-skin borders** to coincident rigid-part vertices so seams close
  (both directions).
- **DOAXBV writes helper bones after some draws**, so while a girl moves a
  part's WORLD0 can lag its bone. Suppression accepts a near match at the
  bone origin and remembers each actor's vertex buffers.
- **Springs need care at cuts.** Reset on hip jumps; cap acceleration.

## Checking tools (`tools/crossport/`)

- `validate_asset.py <asset> <present> <bank>`: poses an asset on the target
  skeleton from a RAM dump and lists parts with stretched triangles, far
  vertices, or unfilled bones.
- `rig_error.py <d3c> <bank> <presents...>`: compares the rig of DOA3 joint
  parts with DOA3's own CPU joints frame by frame.
- `pick_part.py <asset> <present> <bank> <x> <y> [w h]`: which parts cover a
  screenshot pixel.
- `fit_experiment.py`: compares skinning models on held-out frames.
- Runtime switches: `RECOMP_COSTUME_SKIP=a-b` (hide asset parts a..b, for
  bisecting), `RECOMP_COSTUME_HANG=0`, `RECOMP_COSTUME_JIGGLE=<gain>`,
  `RECOMP_COSTUME_DEBUG=1`.
- Headless verification: `RECOMP_SHOT=<presents>` screenshots (DOAXBV debug
  hook, kept out of git) and `RECOMP_D3D_FRAME_DUMP` (DOA3); 30-second clips
  are 1800 consecutive frames encoded with ffmpeg.

## State when paused

DOA3 -> DOAXBV: 23 characters converted from the DOA3 attract loop; Hitomi
and Helena carry every fix above and look close to right; the other 21 have
not been rebuilt or re-checked. Ryu never appears in the attract loop.

DOAXBV -> DOA3: 11 assets (all 8 girls, 3 alternates). Reported in play:

- feet sometimes detach at the ankle (cause not yet found);
- the bikini top and breast are separate soft parts with separate springs and
  masks, so the breast pokes through the top;
- hair is the captured mesh on one bone: blocky, no simulation; chest physics
  is a weak spring approximation;
- Tina's close-up shows the shoulder skin crossing the chest skin;
- story cutscenes reuse fighter banks for extras (a bystander can wear a suit).

## Resuming

- Fix the ankle detachment first: dump the pose, pose the asset offline with
  `pose_dxc.py`, compare foot placement with DOA3's own feet.
- Drive a girl's chest and top from one spring state with one mask.
- Replace captured hair with DOA3-style simulation, or bind it to DOA3's
  unidentified bones if they turn out to be hair/chest physics.
- Rebuild all DOA3 characters with `rebuild-doa3.py` (dense frames plus a calm
  hair window per character) and check each with the tools above.
