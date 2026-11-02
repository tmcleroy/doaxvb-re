#ifndef DOAXBV_RECOMP_STAGE_LIGHTS_ADAPTER_H
#define DOAXBV_RECOMP_STAGE_LIGHTS_ADAPTER_H

#include "runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game's scene lights, readable (docs/stage-lights.md, stage_lights.h):
   0x0017CB50 turns a game light record into a D3D light and 0x0017CE00
   packs the ambient colour, both routed through the manual lookup by the
   recipe. RECOMP_STAGE_LIGHTS_VERIFY=1 runs the generated code beside the
   readable code on each call and logs any difference. */
RecompFunction recomp_stage_lights_lookup_manual(uint32_t guest_address);

#ifdef __cplusplus
}
#endif

#endif
