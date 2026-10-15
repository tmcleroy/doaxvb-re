#ifndef DOAXBV_RECOMP_COSTUME_INJECT_H
#define DOAXBV_RECOMP_COSTUME_INJECT_H

#include "d3d_presenter.h"

#include <stdbool.h>
#include <stdint.h>

/* Experimental cross-game costume: RECOMP_COSTUME_INJECT=<asset> draws a
   converted DOA3 character (D3C1 asset, built offline from DOA3 mesh dumps)
   in place of DOAXBV actors. RECOMP_COSTUME_INJECT_A0..A3 set per-actor
   assets; RECOMP_COSTUME_ACTORS=<mask> picks actors (default 0xF). */

#ifdef __cplusplus
extern "C" {
#endif

bool recomp_costume_inject_enabled(void);

/* Called for each world-transformed draw with its WORLD0, view and
   projection. Returns true when the draw belongs to a replaced fighter and
   must not be submitted; the replacement is submitted on that fighter's
   first draw of the present, using the draw as the render-state template. */
bool recomp_costume_inject_draw(uint32_t present, uint32_t vertex_data, const float world[16],
    const float view[16], const float projection[16],
    const RecompD3dPresenterDrawCommand *template_draw);

#ifdef __cplusplus
}
#endif

#endif
