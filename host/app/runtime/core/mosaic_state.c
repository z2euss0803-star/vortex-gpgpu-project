#include <vxl/mosaic_host.h>
static int owner_ok(const struct mosaic_host_state *h,vxl_u64 owner){
 if(!h||!h->engine)return -EINVAL;
 if(!owner||!h->engine->opened||h->engine->owner!=owner)return -EPERM;
 if(h->first_error)return h->first_error;
 if(h->engine->profile_id!=MOSAIC_PROFILE)return -EXDEV;
 if(h->engine->quarantined)return -EIO;
 return 0;
}
int mosaic_phase(vxl_u32 phase,vxl_u32 width,vxl_u32 height,vxl_u32 *layer,vxl_u32 *op,vxl_u32 *items){
 static const vxl_u32 co[16]={32,64,128,128,128,128,128,128,128,128,128,128,128,64,32,3};
 vxl_u32 cursor=0,w=width,h=height,ci=3;
 if(!layer||!op||!items||width<8||width>1280||height<8||height>960||width%4||height%4)return -EINVAL;
 for(vxl_u32 i=0;i<16;++i){
  vxl_u32 stride=i==1||i==2?2:1,up=i==13||i==14?2:1;
  vxl_u32 ow=(w*up+stride-1)/stride,oh=(h*up+stride-1)/stride,ops[6]={1,2},count=2;
  if(i!=15){ops[count++]=3;ops[count++]=4;}
  if(i<3||i==13||i==14||((i>=3&&i<=12)&&(i&1)))ops[count++]=5;
  if(i>=4&&i<=12&&!(i&1))ops[count++]=6;
  for(vxl_u32 j=0;j<count;++j,++cursor)if(cursor==phase){
   *layer=i;*op=ops[j];
   *items=*op==1?(ci*w*h+255)/256:*op==2?((co[i]+7)/8)*((ow*oh+7)/8):*op==3?co[i]:(co[i]*ow*oh+255)/256;
   return 0;
  }
  w=ow;h=oh;ci=co[i];
 }
 return -ERANGE;
}
int mosaic_frame_begin(struct mosaic_ctx *c,vxl_u64 owner,const vxl_u8 run[16],vxl_u32 layout,vxl_u32 epoch,vxl_u32 diagnostics){
 struct mosaic_host_state *h;int rc;vxl_u8 nz=0;
 if(!c||!c->host||!run)return -EINVAL;
 h=c->host;rc=owner_ok(h,owner);if(rc)return rc;
 if(!h->program||!h->nonce_locked||!h->loaded)return -ENODEV;
 if(h->frame_active||h->job_active||h->evidence_pending||h->engine->job.pinned||h->engine->job.state!=VXL_READY)return -EBUSY;
 if(layout>=h->program->layout_count||diagnostics>1)return -EINVAL;
 for(unsigned i=0;i<16;++i)nz|=run[i];
 if(!nz||!epoch)return -EINVAL;
 if(epoch<=h->epoch||!memcmp(run,h->run_id,16))return -ESTALE;
 c->layout=&h->program->layouts[layout].layout;
 h->owner=owner;h->generation=h->engine->generation;h->epoch=epoch;h->phase=h->first=0;h->sequence=1;
 h->frame_active=1;h->result_bytes=0;h->diagnostics=diagnostics;memcpy(h->run_id,run,16);
 memset(&c->expected,0,sizeof(c->expected));
 c->expected.magic=0x31434d56u;c->expected.version=1;c->expected.bytes=128;
 memcpy(c->expected.run_id,run,16);memcpy(c->expected.binding_tag,h->program->binding_sha,16);
 memcpy(c->expected.layout_tag,h->program->layouts[layout].layout_sha,16);memcpy(c->expected.graph_tag,h->program->layouts[layout].graph_sha,16);
 c->expected.generation=h->generation;c->expected.core_mask=15;c->expected.input_epoch=epoch;c->expected.flags=diagnostics;
 return 0;
}
int mosaic_next_job(struct mosaic_ctx *c,vxl_u32 count,struct mosaic_job_v1 *out){
 struct mosaic_host_state *h;vxl_u32 layer,op,items;int rc;
 if(!c||!c->host||!out)return -EINVAL;
 h=c->host;rc=owner_ok(h,h->owner);if(rc)return rc;
 if(!h->frame_active||h->job_active||h->evidence_pending||h->engine->job.state!=VXL_READY||h->engine->job.pinned)return -EBUSY;
 if(h->generation!=h->engine->generation)return -ESTALE;
 if(h->sequence==0xffffffffu)return -EOVERFLOW;
 rc=mosaic_phase(h->phase,c->layout->width,c->layout->height,&layer,&op,&items);if(rc)return rc;
 if(!count||count>MOSAIC_MAX_BATCH||h->first>=items||count>items-h->first)return -ERANGE;
 c->expected.sequence=h->sequence;c->expected.layer=layer;c->expected.op=op;c->expected.tile_first=h->first;c->expected.tile_count=count;
 c->expected.crc=mosaic_job_crc(&c->expected);c->total_tiles=items;*out=c->expected;return 0;
}
int mosaic_submit_begin(struct mosaic_ctx *c,vxl_u64 owner,vxl_u64 job_id,vxl_u32 jobs_before,const struct mosaic_job_v1 *job,vxl_u64 entered){
 struct mosaic_host_state *h;struct mosaic_job_v1 expected;int rc;
 if(!c||!c->host||!job)return -EINVAL;
 h=c->host;rc=owner_ok(h,owner);if(rc)return rc;
 if(owner!=h->owner)return -EPERM;
 if(!job_id||job_id<=h->last_job_id||jobs_before==0xffffffffu)return -EOVERFLOW;
 rc=mosaic_next_job(c,job->tile_count,&expected);if(rc)return rc;
 rc=mosaic_validate_job(c,job);if(rc)return rc;
 if(memcmp(job,&expected,sizeof(expected)))return -ESTALE;
 /* Preserve this entry before prepare/START; do not reset it on READY reuse. */
 memset(&h->evidence,0,sizeof(h->evidence));h->evidence.args=*job;h->evidence.job_id=job_id;
 h->evidence.state=VXL_RUNNING;h->evidence.observed.submit_enter=entered;
 h->evidence_bytes=0;h->job_active=1;h->last_job_id=job_id;
 h->engine->job=(struct vxl_job){.id=job_id,.state=VXL_RUNNING,.pinned=1,.jobs_before=jobs_before};
 return 0;
}
int mosaic_state_fault(struct mosaic_ctx *c,int result){
 struct mosaic_host_state *h;
 if(!c||!c->host||result>=0)return -EINVAL;
 h=c->host;if(h->first_error)return h->first_error;
 if(!h->engine)return -EINVAL;
 h->first_error=result;h->job_active=0;h->evidence_pending=1;h->evidence_bytes=0;
 h->evidence.result=result;h->evidence.state=VXL_QUARANTINED;h->evidence.elapsed_valid=0;
 h->engine->quarantined=1;h->engine->job.pinned=1;h->engine->job.state=VXL_QUARANTINED;h->engine->job.result=result;
 return result;
}
int mosaic_state_complete(struct mosaic_ctx *c,const struct mosaic_completion *p){
 struct mosaic_host_state *h;const struct vxl_event *e;vxl_u64 entered;int rc=0;
 if(!c||!c->host||!p)return -EINVAL;
 h=c->host;if(h->first_error)return h->first_error;
 if(!h->job_active||h->evidence_pending||!h->engine||h->engine->job.state!=VXL_RUNNING)return -EBUSY;
 entered=h->evidence.observed.submit_enter;h->evidence.observed=*p;e=&p->before_ack;
 if(owner_ok(h,h->owner)||h->generation!=h->engine->generation||p->generation!=h->generation||p->sequence!=h->sequence||e->id!=h->evidence.job_id||e->id!=h->engine->job.id)rc=-ESTALE;
 else if(p->checks!=MOSAIC_REQUIRED_CHECKS||e->kind!=VXL_DONE||!(e->status&2)||((e->status>>8)&7)!=5||(e->status&4)||e->errors||e->pending_w||e->pending_r||e->idle||!e->ready||e->jobs!=h->engine->job.jobs_before+1u||p->exit_valid!=15||!p->ack_idle||!p->ack_ready||p->tex_pending||vxl_cdma_status(p->cdma_status)!=1)rc=-EIO;
 else if(p->elapsed_address!=0xa0000028ULL||!p->elapsed_raw||p->elapsed_raw>=4000000000u||p->watchdog!=4000000000u||p->submit_enter!=entered||p->prepare_enter<p->submit_enter||p->prepare_done<p->prepare_enter||p->start<p->prepare_done||p->done_seen<p->start||p->ack_done<p->done_seen||p->complete<p->ack_done)rc=-ENODATA;
 for(unsigned i=0;i<4&&!rc;++i)if(p->exitcode[i])rc=-EIO;
 if(rc)return mosaic_state_fault(c,rc);
 h->job_active=0;h->evidence_pending=1;h->evidence.elapsed_valid=1;h->evidence.state=VXL_COMPLETED;
 h->engine->job.pinned=0;h->engine->job.state=VXL_COMPLETED;return 0;
}
int mosaic_evidence_delivered(struct mosaic_ctx *c,vxl_u64 owner,vxl_u64 job_id,vxl_u32 offset,vxl_u32 bytes,int ok){
 struct mosaic_host_state *h;
 if(!c||!c->host)return -EINVAL;
 h=c->host;
 /* Quarantine blocks execution and REAP, not the same owner's error export.
  * The ioctl transport must copy first, then report the delivered prefix. */
 if(!h->engine||!owner||!h->engine->opened||h->engine->owner!=owner||owner!=h->owner)return -EPERM;
 if(job_id!=h->evidence.job_id)return -ESTALE;
 if(!h->evidence_pending||h->job_active)return -EBUSY;
 if(!ok)return mosaic_state_fault(c,-EFAULT);
 if(offset!=h->evidence_bytes||!bytes||offset>512u||bytes>512u-offset)return -EINVAL;
 h->evidence_bytes+=bytes;return 0;
}
int mosaic_reap(struct mosaic_ctx *c){
 struct mosaic_host_state *h;int rc;
 if(!c||!c->host)return -EINVAL;
 h=c->host;rc=owner_ok(h,h->owner);if(rc)return rc;
 if(h->generation!=h->engine->generation)return -ESTALE;
 if(h->job_active||!h->evidence_pending||h->evidence_bytes!=512u||h->engine->job.state!=VXL_COMPLETED||h->engine->job.pinned)return -EBUSY;
 h->first+=h->evidence.args.tile_count;
 if(h->first==c->total_tiles){h->first=0;++h->phase;}
 ++h->sequence;h->evidence_pending=0;h->engine->job=(struct vxl_job){.state=VXL_READY};return 0;
}
int mosaic_result_delivered(struct mosaic_ctx *c,vxl_u64 owner,vxl_u32 offset,vxl_u32 bytes,int ok){
 struct mosaic_host_state *h;vxl_u32 total;int rc;
 if(!c||!c->host)return -EINVAL;
 h=c->host;rc=owner_ok(h,owner);if(rc)return rc;
 if(owner!=h->owner)return -EPERM;
 if(!h->frame_active||h->phase!=77||h->job_active||h->evidence_pending)return -EBUSY;
 if(!ok)return mosaic_state_fault(c,-EFAULT);
 total=c->layout->width*c->layout->height*3u*4u;
 if(offset!=h->result_bytes||!bytes||offset>total||bytes>total-offset)return -EINVAL;
 h->result_bytes+=bytes;return 0;
}
int mosaic_frame_end(struct mosaic_ctx *c,vxl_u64 owner){
 struct mosaic_host_state *h;int rc;
 if(!c||!c->host)return -EINVAL;
 h=c->host;rc=owner_ok(h,owner);if(rc)return rc;
 if(owner!=h->owner)return -EPERM;
 if(h->generation!=h->engine->generation)return -ESTALE;
 if(!h->frame_active||h->phase!=77||h->job_active||h->evidence_pending||h->engine->job.state!=VXL_READY||h->engine->job.pinned||h->result_bytes!=c->layout->width*c->layout->height*12u)return -EBUSY;
 h->frame_active=0;return 0;
}
