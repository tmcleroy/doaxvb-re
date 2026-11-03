#ifndef DOAXBV_RECOMP_BODY_MORPH_ADAPTER_H
#define DOAXBV_RECOMP_BODY_MORPH_ADAPTER_H

#include "runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game's blend of one morph record (a girl's hand gesture, face or
   chest), readable (docs/body-morph.md, body_morph.h). Registered for
   0x00064FD0, which the recipe routes through the manual lookup.
   RECOMP_BODY_MORPH_VERIFY=1 checks it against the generated code each
   call. */
RecompFunction recomp_body_morph_lookup_manual(uint32_t guest_address);

/* Holds every girl's eyes and mouth at one of their face shapes (the free
   camera's Eyes and Mouth rows): the shape's number in her eye set (6
   shapes: 0 open, 1 closed, 4 a wink) or mouth set (10), or -1 to leave
   the game's own. */
void recomp_body_morph_hold_face(int eyes, int mouth);

#ifdef __cplusplus
}
#endif

#endif
