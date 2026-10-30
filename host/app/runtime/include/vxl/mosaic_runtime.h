#ifndef VXL_MOSAIC_RUNTIME_H
#define VXL_MOSAIC_RUNTIME_H
#include <vxl/mosaic_host.h>
#include <uapi/vx_mosaic.h>
int mosaic_runtime_attach(struct mosaic_ctx *,const struct mosaic_io *);
int mosaic_io_bytes(struct mosaic_ctx *,vxl_u64,void *,vxl_u64,int);
int mosaic_runtime_idle(struct mosaic_ctx *);
int mosaic_runtime_seal(struct mosaic_ctx *,const struct vxl_mosaic_seal_v1 *);
int mosaic_runtime_begin(struct mosaic_ctx *,const struct vxl_mosaic_frame_v1 *);
int mosaic_runtime_end(struct mosaic_ctx *,const struct vxl_mosaic_frame_v1 *);
int mosaic_prepare(struct vxl_engine *,struct vxl_prepared *);
int mosaic_runtime_start(struct mosaic_ctx *);
int mosaic_runtime_poll(struct vxl_engine *);
int mosaic_complete(struct vxl_engine *,const struct vxl_event *);
int mosaic_evidence_encode(const struct mosaic_ctx *,vxl_u8 out[512]);
int mosaic_uapi_validate(vxl_u32,const struct vxl_request *,vxl_u32,int);
int mosaic_engine_call(struct vxl_engine *,vxl_u64,vxl_u32,struct vxl_request *);
void mosaic_engine_close(struct vxl_engine *,vxl_u64);
void mosaic_delivery_capture(struct mosaic_ctx *,struct mosaic_delivery *);
int mosaic_delivery_finish(struct mosaic_ctx *,const struct mosaic_delivery *,int);
#endif
