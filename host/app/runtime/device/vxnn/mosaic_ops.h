#pragma once
#include "device/mosaic_s8/graph.h"
// Trusted, user-owned primitive boundary. This is neither the sealed wire
// argument nor an ioctl. Model layer index/topology is deliberately absent.
enum VxnnMosaicOpcode : uint32_t {VXNN_MOSAIC_QUANT=1,VXNN_MOSAIC_CONV=2,VXNN_MOSAIC_STATS=3,VXNN_MOSAIC_APPLY=4,VXNN_MOSAIC_RELU=5,VXNN_MOSAIC_ADD=6,VXNN_MOSAIC_NORM_RELU_QUANT=7,VXNN_MOSAIC_NORM_ADD_QUANT=8};
struct VxnnMosaicGeometry {uint32_t ci,co,h,w,k,stride,upsample,kpad;};
struct VxnnMosaicPrimitive {uint32_t opcode,first,count;VxnnMosaicGeometry geometry;float scale;bool save_input;bool separate_input=false;uint32_t schedule_version=1,weight_layout_version=1;};
template<class T>struct VxnnMosaicSpan {T*data=nullptr;size_t count=0;};
struct VxnnMosaicBuffers {
 VxnnMosaicSpan<const float>input,coeff,gamma,beta;
 VxnnMosaicSpan<const int16_t>weights;
 VxnnMosaicSpan<int16_t>quant,a,b;
 VxnnMosaicSpan<float>output,skip,stats;
 VxnnMosaicSpan<int64_t>acc;
 mosaic_norm_workspace norm{};
};
// Every call owns only [first,first+count), with global item%4 assignment.
// Convolution calls the original signed INT16/I48 PE protocol, never WMMA,
// a float matrix fallback, a changed reduction, or a raised watchdog.
int vxnn_mosaic_primitive(const VxnnMosaicPrimitive*,const VxnnMosaicBuffers*,uint32_t core,const mosaic_s8_ops*);
