#ifndef MOSAIC_DEVICE_ENTRY_H
#define MOSAIC_DEVICE_ENTRY_H
#include "graph.h"
struct mosaic_entry_catalog {
 uint32_t width,height;
 unsigned char layout_tag[16],graph_tag[16];
 mosaic_layout layout;
 mosaic_parameters parameters[16];
};
int mosaic_entry_layout(uint32_t,uint32_t,mosaic_layout *);
int mosaic_entry_prepare(const mosaic_job_v1 *,const mosaic_entry_catalog *,size_t,uint32_t,mosaic_device_context *);
int mosaic_entry_metadata(const mosaic_job_v1 *,uint32_t,unsigned char[64]);
#endif
