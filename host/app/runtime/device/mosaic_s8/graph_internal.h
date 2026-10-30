#ifndef MOSAIC_GRAPH_INTERNAL_H
#define MOSAIC_GRAPH_INTERNAL_H
#include "graph.h"
#include <errno.h>
static inline bool mosaic_saved(uint32_t i){return i>=3&&i<=11&&(i&1);}
static inline bool mosaic_residual(uint32_t i){return i>=4&&i<=12&&!(i&1);}
static inline bool mosaic_relu(uint32_t i){return i<3||i==13||i==14||mosaic_saved(i);}
static inline bool mosaic_leader(const mosaic_device_context *c){return c->io[c->core].participant==0||c->io[c->core].participant==4;}
static inline int mosaic_share(mosaic_device_context *c,int rc){auto &io=c->io[c->core];return io.participant==4?rc:io.broadcast?io.broadcast(io.context,rc):-EINVAL;}
static inline uint32_t mosaic_oh(const mosaic_layer &l){return (l.h*l.upsample+l.stride-1)/l.stride;}
static inline uint32_t mosaic_ow(const mosaic_layer &l){return (l.w*l.upsample+l.stride-1)/l.stride;}
int mosaic_convolve_range(const mosaic_job_v1 *,mosaic_device_context *,const mosaic_layer *);
int mosaic_point_range(const mosaic_job_v1 *,mosaic_device_context *,const mosaic_layer *);
#endif
