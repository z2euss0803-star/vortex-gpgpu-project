#pragma once
#include "mosaic_wire.h"
#include "mosaic_work_layout_v1.h"
/* Same private128/8refs packet. ABI must be3 for every opcode1..8.
 * The inherited field named reserved (word31/offset124) is options ONLY in ABI3:
 * bit0 enables per-core CSR64 timing, all other bits must be zero.
 * Old ABI1/2 sources and meanings are unchanged. This component has no entry,
 * published CODE, host lowering or hardware execution claim. */
static_assert(sizeof(VxnnMosaicArgs)==128&&offsetof(VxnnMosaicArgs,reserved)==124,"ABI3 options word124");
int vxnn_mosaic_validate_coop_v3(const VxnnMosaicArgs*,bool relocated,VxnnMosaicShape*);
