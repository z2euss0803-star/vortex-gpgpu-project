#ifndef VXNN_MOSAIC_COOP_H
#define VXNN_MOSAIC_COOP_H
#include "mosaic_ops.h"
// Portable arithmetic/collective layer for the separately sealed ABI3 entry.
// The caller validates the complete wire and exact owned WORK before this API.
int vxnn_mosaic_coop_primitive(const VxnnMosaicPrimitive*,const VxnnMosaicBuffers*,uint32_t,const mosaic_s8_ops*);
#endif
