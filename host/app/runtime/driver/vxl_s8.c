#include "vxl_internal.h"
#ifdef VXL_S8_SOURCE_BOUND
#include <vxl_s8_release.h>
static vxl_u64 s8_monotonic_ns(void *p){(void)p;return ktime_get_ns();}
#endif
int vxl_s8_candidate(struct vxl_device *d,struct vxl_engine *engine,struct vxl_s8_ctx *ctx) {
#ifdef VXL_S8_SOURCE_BOUND
 int rc;
 rc=vxl_s8_context_init(ctx,engine,vxl_s8_frozen_program.binding_sha);if(rc)return rc;
 ctx->io.context=d;ctx->io.rw=vxl_private_rw;ctx->io.now_ns=s8_monotonic_ns;
 rc=vxl_s8_program_register(ctx,&vxl_s8_frozen_program);
 if(rc){engine->s8=NULL;return rc;}
 return 0;
#else
 /* A compile without the final binding is useful software evidence only.
  * In particular it cannot probe with old W4 bit/XSA placeholder digests. */
 (void)d;(void)engine;(void)ctx;return -ENODEV;
#endif
}

int vxl_s8_attach(struct vxl_device *d){return vxl_s8_candidate(d,&d->engine,&d->s8);}
