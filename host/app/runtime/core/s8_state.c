#include <vxl/s8.h>
#include <vxl/engine.h>
static int fault(struct vxl_s8_ctx *c,vxl_u32 classification,int rc) {c->error_class=classification;return rc;}
static int require_capture(struct vxl_s8_ctx *c,vxl_u64 gpu,vxl_u32 bytes) {
 if(c->capture_count>=192)return -ENOSPC;
 c->capture[c->capture_count++]=(struct vxl_s8_capture){gpu,bytes,0};return 0;
}
static int capture_plan(struct vxl_s8_ctx *c) {
 int rc;c->capture_count=0;
 for(unsigned slot=0;slot<16;++slot){rc=require_capture(c,0x81400000ULL+4096*slot,4096);if(rc)return rc;}
 if(require_capture(c,0x81410000,4096) || require_capture(c,0x81411000,4096))return -ENOSPC;
 if(require_capture(c,0x800f0000,64) || require_capture(c,0x800f0040,64) || require_capture(c,0x800fffc0,64))return -ENOSPC;
 for(unsigned hart=0;hart<64;++hart){rc=require_capture(c,0xffff0000ULL-(hart+1)*8192ULL,64);if(rc)return rc;}
 if(require_capture(c,0xffff0000,64))return -ENOSPC;
 for(unsigned which=0;which<2;++which)for(unsigned slot=0;slot<16;++slot) {
  vxl_u64 base=(which?0x81200000ULL:0x81000000ULL)+slot*0x20000ULL;
  if(require_capture(c,base,64) || require_capture(c,base+64+vxl_s8_input_bytes(&c->job_args,which),64) || require_capture(c,base+0x1ffc0,64))return -ENOSPC;
 }
 return c->capture_count==182?0:-EINVAL;
}
int vxl_s8_complete(struct vxl_engine *e,const struct vxl_event *event) {
 struct vxl_s8_ctx *c;struct vxl_s8_hw_snapshot *s;int rc;
 if(!e || !e->s8 || !event)return -EINVAL;
 c=e->s8;s=&c->hw;
 if(c->times.invalid)return fault(c,VXL_S8_TIMING_INVALID,-ENODATA);
 if(c->error_class)return fault(c,c->error_class,-EIO);
 if(!s->identity_valid||!s->metadata_valid)return fault(c,VXL_S8_IDENTITY_MISMATCH,-ESTALE);
 {vxl_u32 required=0;if(c->job_args.case_id!=1)for(unsigned core=0;core<4;++core)required+=!!(c->job_args.core_mask&(1u<<core));if(s->metadata_count!=required)return fault(c,VXL_S8_IDENTITY_MISMATCH,-ESTALE);}
 if(c->delivery_fault || c->capture_pending || e->job.state!=VXL_RUNNING)return -EBUSY;
 if(s->job_id!=e->job.id || s->event.id!=e->job.id || event->id!=e->job.id || s->generation!=e->generation || s->sequence!=c->job_args.sequence || c->job_args.generation!=e->generation)return fault(c,VXL_S8_IDENTITY_MISMATCH,-ESTALE);
 if(s->jobs_before!=e->job.jobs_before)return fault(c,VXL_S8_RUNTIME_FAULT,-ESTALE);
 if(!s->elapsed_valid || !s->pre_ack || s->elapsed_address!=0xa0000028ULL || !s->elapsed_raw || s->elapsed_raw>=4000000000u || s->watchdog!=4000000000u)return fault(c,VXL_S8_TIMING_INVALID,-ENODATA);
 if(!(event->status&2) || ((event->status>>8)&7)!=5 || s->event.idle || event->errors || (event->status&4) || event->pending_w || event->pending_r || event->jobs!=(vxl_u32)(e->job.jobs_before+1) || !event->ready || !event->idle)return fault(c,VXL_S8_RUNTIME_FAULT,-EIO);
 if(s->event.status!=event->status || s->event.errors!=event->errors || s->event.pending_w!=event->pending_w || s->event.pending_r!=event->pending_r || s->event.jobs!=event->jobs || s->event.ready!=event->ready)return fault(c,VXL_S8_RUNTIME_FAULT,-EIO);
 if(s->exit_valid!=15 || !s->guard_valid || !s->immutable_valid || !s->input_valid || !s->ack_valid || !s->ack_idle || !s->ack_ready || s->tex_pending || vxl_cdma_status(s->cdma_status)!=1)return fault(c,VXL_S8_RUNTIME_FAULT,-EIO);
 for(unsigned core=0;core<4;++core)if(s->exitcode[core])return fault(c,VXL_S8_RUNTIME_FAULT,-EIO);
 for(unsigned index=0;index<3;++index)for(unsigned j=0;j<64;++j)if(s->crt_guards[64*index+j]!=vxl_s8_guard_byte(9,0,index,j))return fault(c,VXL_S8_RUNTIME_FAULT,-EIO);
 rc=capture_plan(c);if(rc)return fault(c,VXL_S8_RESOURCE_REFUSED,rc);
 c->capture_pending=1;c->trace_delivered=0;return 0;
}
int vxl_s8_capture_range(struct vxl_s8_ctx *c,vxl_u64 gpu,vxl_u64 bytes) {
 if(!c || !c->engine || !c->capture_pending || c->delivery_fault)return -EBUSY;
 if(c->generation!=c->engine->generation || !c->engine->opened)return -ESTALE;
 if(!bytes || gpu<0x80000000ULL || gpu>=0x100000000ULL || bytes>0x100000000ULL-gpu)return -EINVAL;
 for(vxl_u32 i=0;i<c->capture_count;++i) {
  struct vxl_s8_capture *part=&c->capture[i];vxl_u64 cursor=part->gpu+part->delivered,end=part->gpu+part->bytes;
  /* A gap never earns delivery: repeated/overlapping reads are safe, out-of-order
   * disjoint fragments must be read again after their prefix has arrived. */
  if(gpu<=cursor && gpu+bytes>cursor){vxl_u64 delivered_end=gpu+bytes;if(delivered_end>end)delivered_end=end;part->delivered=(vxl_u32)(delivered_end-part->gpu);}
 }
 return 0;
}
int vxl_s8_can_reap(const struct vxl_s8_ctx *c) {
 if(!c || !c->engine || !c->capture_pending || c->delivery_fault || c->error_class || c->trace_delivered!=7 || c->capture_count!=182)return -EBUSY;
 if(c->engine->quarantined || c->engine->job.pinned || c->engine->job.state!=VXL_COMPLETED)return -EBUSY;
 for(vxl_u32 i=0;i<c->capture_count;++i)if(c->capture[i].delivered!=c->capture[i].bytes)return -EBUSY;
 return 0;
}
void vxl_s8_reaped(struct vxl_s8_ctx *c) {c->capture_pending=0;}
void vxl_s8_delivery_fault(struct vxl_s8_ctx *c,int rc) {
 if(!c || !c->engine || !c->engine->job.id || c->delivery_fault)return;
 c->delivery_fault=1;if(!c->error_class)c->error_class=VXL_S8_RUNTIME_FAULT;
 c->engine->quarantined=1;c->engine->job.pinned=1;c->engine->job.state=VXL_QUARANTINED;if(!c->engine->job.result)c->engine->job.result=rc;
}
