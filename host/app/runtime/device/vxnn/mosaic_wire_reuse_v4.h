#pragma once
#include "mosaic_wire_coop_v3.h"
// Independent private ABI4, still128 bytes/eight refs/options0..1.
// WORK physical layout1 and profile wire2 remain unchanged. The entry fixes
// schedule2 and weight-layout2 explicitly; equal extents never imply a layout.
enum {MOSAIC_PRIVATE_ABI_REUSE_V4=4,MOSAIC_SCHEDULE_SPATIAL_MAJOR_NB_MOD4_V2=2,MOSAIC_WEIGHT_MB_KPAD_ROW8_I16_V2=2};
static_assert(sizeof(VxnnMosaicArgs)==128&&offsetof(VxnnMosaicArgs,reserved)==124,"ABI4 packet/options");
static inline int mosaic_reuse_work_layout_v4(uint32_t op,uint32_t kpad,uint32_t tree,MosaicWorkLayoutV1*out){
 MosaicWorkLayoutV1 layout{};int rc=mosaic_work_layout_v1(op,kpad,tree,&layout);if(rc||!out)return rc?rc:-EINVAL;
 layout.schedule_version=MOSAIC_SCHEDULE_SPATIAL_MAJOR_NB_MOD4_V2;
 layout.weight_layout_version=MOSAIC_WEIGHT_MB_KPAD_ROW8_I16_V2;
 *out=layout;return 0;
}
int vxnn_mosaic_validate_reuse_v4(const VxnnMosaicArgs*,bool relocated,VxnnMosaicShape*);
