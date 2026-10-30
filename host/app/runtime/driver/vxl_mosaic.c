#include "vxl_internal.h"
#ifdef VXL_MOSAIC_SOURCE_BOUND
#include <vxl_mosaic_release.h>
static vxl_u32 mosaic_reg_read(void *p,vxl_u32 offset){struct vxl_device *d=p;return readl(d->gpu+offset);}
static void mosaic_reg_write(void *p,vxl_u32 offset,vxl_u32 value){struct vxl_device *d=p;if(offset==8&&value==1)dma_wmb();writel(value,d->gpu+offset);}
static int mosaic_ready(void *p){struct vxl_device *d=p;return gpiod_get_value_cansleep(d->ready);}
static vxl_u32 mosaic_cdma(void *p){struct vxl_device *d=p;return readl(d->cdma+4);}
static vxl_u64 mosaic_clock(void *p){(void)p;return ktime_get_ns();}
#endif
int vxl_mosaic_candidate(struct vxl_device *d,struct vxl_engine *engine,struct mosaic_ctx *ctx,struct mosaic_host_state *state){
#ifdef VXL_MOSAIC_SOURCE_BOUND
 int rc=mosaic_host_init(ctx,state,engine,vxl_mosaic_frozen_program.binding_sha);if(rc)return rc;
 rc=mosaic_program_register(ctx,&vxl_mosaic_frozen_program);if(rc)return rc;
 const struct mosaic_io io={d,vxl_private_rw,mosaic_reg_read,mosaic_reg_write,mosaic_ready,mosaic_cdma,mosaic_clock};
 rc=mosaic_runtime_attach(ctx,&io);if(!rc)rc=mosaic_runtime_idle(ctx);if(rc)engine->mosaic=NULL;return rc;
#else
 /* An unbound module is compile evidence; it cannot admit a fake program. */
 (void)d;(void)engine;(void)ctx;(void)state;return -ENODEV;
#endif
}
int vxl_mosaic_attach(struct vxl_device *d){return vxl_mosaic_candidate(d,&d->engine,&d->mosaic,&d->mosaic_state);}
