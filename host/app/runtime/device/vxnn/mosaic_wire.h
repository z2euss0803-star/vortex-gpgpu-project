#pragma once
#include <stddef.h>
#include <stdint.h>
// Private NEW program0x10020 args; never the old program12 job/UAPI.
enum VxnnMosaicRef {VXM_INPUT,VXM_OUTPUT,VXM_QUANT,VXM_WEIGHTS,VXM_COEFF,VXM_STATS,VXM_SKIP,VXM_WORK};
struct VxnnMosaicArgs {
 uint32_t abi,bytes,opcode,flags,address[8],extent[8];
 uint32_t ci,co,h,w,k,stride,upsample,kpad,first,count,scale_bits,reserved;
};
struct VxnnMosaicShape {uint32_t required[8],items,used_mask,core_work_bytes,norm_elements;};
static_assert(sizeof(VxnnMosaicArgs)==128&&offsetof(VxnnMosaicArgs,address)==16&&offsetof(VxnnMosaicArgs,extent)==48&&offsetof(VxnnMosaicArgs,ci)==80&&offsetof(VxnnMosaicArgs,first)==112,"new Mosaic private128 offsets");
// flags bit0 is explicit quant+skip save; bit1 is separate-input point ops.
// Unused references are exactly the owned WORK prefix64, never null handles.
// WORK actual arithmetic begins after that prefix. Output shape is published
// only after validation; CPU pointers/DMA/GPU addresses remain distinct.
int vxnn_mosaic_validate(const VxnnMosaicArgs*,bool relocated,VxnnMosaicShape*);

// ABI2 fused operations7/8; ABI1 operations retain their original validator.
int vxnn_mosaic_validate_fused(const VxnnMosaicArgs*,bool,VxnnMosaicShape*);
