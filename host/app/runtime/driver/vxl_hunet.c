#include "vxl_internal.h"
#include <linux/vmalloc.h>
#ifdef VXL_HYBRID_SOURCE_BOUND
#include <vxl_hybrid_release.h>
#endif
#ifdef VXL_FUSED_SOURCE_BOUND
#include <vxl_fused_release.h>
#endif
#ifdef VXL_OPT_SOURCE_BOUND
#include <vxl_opt_release.h>
#endif
static u64 monotonic_ns(void *p){(void)p;return ktime_get_ns();}
static void release_snapshot(void *p){kvfree(p);}
int vxl_hunet_candidate(struct vxl_device *d,struct vxl_device *stage,void *snapshot){
 int rc;if(!d||!stage||!snapshot)return -EINVAL;
 vxl_hunet_release_defaults(&stage->release);
 /* An ordinary module compile is not a release-bound production image. */
 if(!memchr_inv(stage->release.binding.source_sha256,0,32))return -ESTALE;
 rc=vxl_hunet_layout_init(&stage->hunet,&stage->engine,&stage->release);if(rc)return rc;
 stage->hunet.input_snapshot=snapshot;stage->hunet.io.context=d;stage->hunet.io.rw=vxl_private_rw;stage->hunet.io.now_ns=monotonic_ns;
#ifdef VXL_HYBRID_SOURCE_BOUND
 memcpy(&stage->hybrid_release.binding,vxl_hybrid_binding_bytes,384);vxl_hunet_hash(&stage->hybrid_release.binding,384,stage->hybrid_release.binding_sha);
 rc=vxl_hybrid_init(&stage->hunet,&stage->hybrid_release);if(rc)return rc;stage->hunet.hybrid.hw_check=vxl_hybrid_hw_idle;
#endif
#ifdef VXL_FUSED_SOURCE_BOUND
 stage->fused_release=vxl_fused_release_bound;
 rc=vxl_fused_init(&stage->hunet,&stage->fused_release);if(rc)return rc;stage->hunet.hybrid.hw_check=vxl_hybrid_hw_idle;
#endif
#ifdef VXL_OPT_SOURCE_BOUND
 stage->opt_release=vxl_opt_release_bound;
 rc=vxl_opt_init(&stage->hunet,&stage->opt_release);if(rc)return rc;
 stage->hunet.digest_backend=stage->opt_release.digest_backend;stage->hunet.digest_enabled=1;stage->hunet.hybrid.hw_check=vxl_hybrid_hw_idle;
#endif
 return 0;
}
int vxl_hunet_attach(struct vxl_device *d){
 void *snapshot=kvzalloc(196608,GFP_KERNEL);int rc;if(!snapshot)return -ENOMEM;
 rc=vxl_hunet_candidate(d,d,snapshot);if(rc){kvfree(snapshot);d->hunet.input_snapshot=NULL;return rc;}
 return devm_add_action_or_reset(d->dev,release_snapshot,snapshot);
}
