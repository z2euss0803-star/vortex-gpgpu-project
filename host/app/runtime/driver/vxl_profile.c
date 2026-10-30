#include "vxl_internal.h"
#include <vxl/profile_switch.h>
#ifdef __KERNEL__
#include <linux/slab.h>
#include <linux/vmalloc.h>
#endif
static int profile_idle(void *context,struct vxl_engine *e){
 struct vxl_device *d=context;(void)e;
 if(readl(d->gpu)!=0x56580204||readl(d->gpu+4)!=0x40404||readl(d->gpu+0x30)!=15||readl(d->gpu+0x38)!=0x10001)return -ENODEV;
 vxl_u32 status=readl(d->gpu+0xc),cdma=readl(d->cdma+4);
 /* Selection starts no transfer: IOC may still be clear after cold boot.
  * Idle plus no error is required here. Transfer completion continues to
  * require Idle+IOC in vxl_cdma_status(), unchanged. */
 if(gpiod_get_value_cansleep(d->ready)!=1||!(readl(d->gpu+0x34)&1)||((status>>8)&7)||(status&6)||readl(d->gpu+0x1c)||readl(d->gpu+0x20)||readl(d->gpu+0x24)||(readl(d->gpu+0x58)&1)||(cdma&0x4770)||!(cdma&2))return -EBUSY;
 return 0;
}
static void discard(void *context,struct vxl_engine *next){struct vxl_device *d=context,*stage=d->profile_candidate;if(!stage||next!=&stage->engine)return;kvfree(stage->hunet.input_snapshot);kvfree(stage);d->profile_candidate=NULL;}
static int prepare(void *context,vxl_u32 profile,vxl_u32 generation,struct vxl_engine **out){
 struct vxl_device *d=context;if(d->profile_candidate)return -EBUSY;
 struct vxl_device *stage=kvzalloc(sizeof(*stage),GFP_KERNEL);if(!stage)return -ENOMEM;d->profile_candidate=stage;
 int rc=vxl_engine_init_profile(&stage->engine,d->engine.ops,d->engine.context,generation,profile);
 if(!rc){stage->engine.snapshot.idle=stage->engine.snapshot.ready=1;stage->engine.snapshot.jobs=readl(d->gpu+0x2c);
  if(profile==5)rc=vxl_s8_candidate(d,&stage->engine,&stage->s8);
  else if(profile==6)rc=vxl_mosaic_candidate(d,&stage->engine,&stage->mosaic,&stage->mosaic_state);
  else if(profile==4){void *snapshot=kvzalloc(196608,GFP_KERNEL);if(!snapshot)rc=-ENOMEM;else{rc=vxl_hunet_candidate(d,stage,snapshot);if(!rc&&(!stage->hunet.fused.release||stage->hunet.opt.release))rc=-ESTALE;if(rc)kvfree(snapshot);}}
  else rc=-EINVAL;
 }
 if(rc){/* candidate init owns the snapshot only on success */if(profile==4)stage->hunet.input_snapshot=NULL;discard(d,&stage->engine);return rc;}
 *out=&stage->engine;return 0;
}
static void commit(void *context,struct vxl_engine *next){
 struct vxl_device *d=context,*stage=d->profile_candidate;vxl_u32 profile=d->engine.profile_id;
 if(!stage||next!=&stage->engine)return; /* trusted factory identity, checked before commit */
 kvfree(d->hunet.input_snapshot);memset(&d->hunet,0,sizeof(d->hunet));
 if(profile==4){d->release=stage->release;d->hybrid_release=stage->hybrid_release;d->fused_release=stage->fused_release;d->opt_release=stage->opt_release;d->hunet=stage->hunet;stage->hunet.input_snapshot=NULL;
  d->hunet.engine=&d->engine;d->hunet.release=&d->release;d->hunet.io.context=d;
  if(d->hunet.hybrid.release)d->hunet.hybrid.release=&d->hybrid_release;
  if(d->hunet.fused.release)d->hunet.fused.release=&d->fused_release;
  if(d->hunet.opt.release)d->hunet.opt.release=&d->opt_release;
  d->engine.hunet=&d->hunet;
 }else if(profile==5){struct vxl_s8_trace *trace=d->s8.trace;int (*observe)(struct vxl_s8_ctx *,vxl_u32,vxl_u64,vxl_u64,vxl_u64,int)=d->s8.observe;d->s8=stage->s8;d->s8.engine=&d->engine;d->s8.trace=trace;d->s8.observe=observe;d->engine.s8=&d->s8;vxl_s8_owner_open(&d->s8);vxl_s8_trace_new_epoch(&d->s8);
 }else{d->mosaic=stage->mosaic;d->mosaic_state=stage->mosaic_state;d->mosaic.host=&d->mosaic_state;d->mosaic_state.engine=&d->engine;d->mosaic_state.owner=d->engine.owner;d->mosaic_state.generation=d->engine.generation;d->engine.mosaic=&d->mosaic;}
 kvfree(stage);d->profile_candidate=NULL;
}
int vxl_profile_setup(struct vxl_device *d){if(!d||d->profile_candidate||d->profile_factory.enabled)return -EINVAL;d->profile_factory=(struct vxl_profile_factory){1,d,profile_idle,prepare,discard,commit};d->engine.profile_factory=&d->profile_factory;return 0;}
int vxl_profile_select(struct vxl_device *d,vxl_u64 owner,struct vxl_request *r){
 if(!d)return -EINVAL;
 int rc=vxl_select_request(&d->engine,owner,r);
#ifdef __KERNEL__
 if(rc==-EBUSY){
  unsigned objects=0;for(unsigned i=0;i<VXL_MAX_OBJECTS;i++)if(d->engine.objects[i].handle||d->engine.objects[i].allocated||d->engine.objects[i].storage)objects++;
  dev_warn_ratelimited(d->dev,"select refused rc=%d target=%u profile=%u generation=%u opened=%u owner_match=%u state=%u job=%llu pinned=%u objects=%u candidate=%u\n",rc,r?r->profile_id:0,d->engine.profile_id,d->engine.generation,d->engine.opened,owner==d->engine.owner,d->engine.job.state,(unsigned long long)d->engine.job.id,d->engine.job.pinned,objects,d->profile_candidate!=NULL);
  dev_warn_ratelimited(d->dev,"select hardware gpio=%d ready=%08x status=%08x errors=%08x pending_w=%u pending_r=%u tex=%08x cdma_cr=%08x cdma_sr=%08x\n",gpiod_get_value_cansleep(d->ready),readl(d->gpu+0x34),readl(d->gpu+0xc),readl(d->gpu+0x1c),readl(d->gpu+0x20),readl(d->gpu+0x24),readl(d->gpu+0x58),readl(d->cdma),readl(d->cdma+4));
  dev_warn_ratelimited(d->dev,"select software s8_capture=%u s8_delivery=%u s8_error=%u s8_export=%d hunet_active=%u hunet_end=%u cpu_owner=%u mosaic_loaded=%u mosaic_frame=%u mosaic_job=%u mosaic_evidence=%u\n",d->s8.capture_pending,d->s8.delivery_fault,d->s8.error_class,vxl_s8_trace_open_allowed(&d->s8),d->hunet.active,d->hunet.end_pending,d->hunet.hybrid.owner!=0,d->mosaic_state.loaded,d->mosaic_state.frame_active,d->mosaic_state.job_active,d->mosaic_state.evidence_pending);
 }
#endif
 return rc;
}
