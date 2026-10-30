#ifndef VXL_MOSAIC_LAYOUT_H
#define VXL_MOSAIC_LAYOUT_H
#include <vxl/mosaic_types.h>
enum mosaic_role {MOSAIC_CODE=0,MOSAIC_WEIGHTS,MOSAIC_FP_A,MOSAIC_FP_B,MOSAIC_Q_INPUT,MOSAIC_SKIP,MOSAIC_NORM_TREE,MOSAIC_STATS,MOSAIC_TILE_SCRATCH,MOSAIC_ROLES};
struct mosaic_region {mosaic_u64 address,bytes;mosaic_u32 guard,readonly;};
struct mosaic_layout {mosaic_u32 width,height;struct mosaic_region region[MOSAIC_ROLES];mosaic_u64 end;};
#ifdef __cplusplus
extern "C" {
#endif
int mosaic_layout_check(const struct mosaic_layout *);
#ifdef __cplusplus
}
#endif
#endif
