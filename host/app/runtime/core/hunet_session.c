#include <vxl/engine.h>
#include <vxl/hunet.h>
#include "../generated/hunet/assets.h"
#include "../generated/hunet_fused/layout.h"
static struct vxl_object *find(struct vxl_engine *e,vxl_u64 h){unsigned i;for(i=0;i<64;i++)if(h&&e->objects[i].handle==h)return &e->objects[i];return 0;}
static int failure(struct vxl_hunet_ctx *c,int rc){if(rc==-EIO||rc==-ETIMEDOUT){c->engine->quarantined=1;c->engine->job.state=VXL_QUARANTINED;c->engine->job.pinned=1;c->engine->job.result=rc;}return rc;}
int vxl_hunet_seal_mode(struct vxl_hunet_ctx *c,const struct vxl_hunet_seal_v1 *s,vxl_u32 mode,const vxl_u8 binding_sha[32]){
 struct vxl_engine *e=c->engine;struct vxl_object *o;vxl_u64 stamp,handles[4],address[4]={VXL_HUNET_CODE,VXL_HUNET_WEIGHTS,VXL_HUNET_WS,0},size[4]={8388608,67108864,VXL_HUNET_WS_BYTES,128};vxl_u32 purpose[4]={VXL_CODE,VXL_WEIGHTS,VXL_WORKSPACE,VXL_ARGS};unsigned i;vxl_u8 sha[32];int rc;
 rc=vxl_hunet_payload_validate(VXL_HUNET_SEAL,s,128);if(rc)return rc;
 if(c->sealed||c->active||e->job.state!=VXL_READY)return -EBUSY;
 if(mode!=6&&mode!=9&&mode!=10)return -EINVAL;
 if(memcmp(s->binding_sha256,binding_sha,32))return -ESTALE;
 handles[0]=s->code;handles[1]=s->weights;handles[2]=s->workspace;handles[3]=s->args;
 for(i=0;i<4;i++){o=find(e,handles[i]);if(!o||o->purpose!=purpose[i]||o->length!=size[i]||o->gpu!=address[i]||(o->handle>>32)!=e->generation)return -EBADF;}
 c->program_id=mode;
 if(mode==9){stamp=vxl_hunet_mgmt_now(c);rc=vxl_fused_restore_descriptors(c);vxl_hunet_mgmt_mark(c,VXL_MGMT_DESC_RESTORE,stamp,VXL_FUSED_DESCRIPTORS_BYTES);if(rc)return failure(c,rc);}
 if(mode==10){stamp=vxl_hunet_mgmt_now(c);rc=vxl_opt_restore_descriptors(c);vxl_hunet_mgmt_mark(c,VXL_MGMT_DESC_RESTORE,stamp,c->opt.release->catalog->matrix_bytes+c->opt.release->catalog->graph_bytes);if(rc)return failure(c,rc);}
 rc=vxl_hunet_check_program(c,1);if(rc)return failure(c,rc);
 rc=vxl_hunet_hash_gpu(c,VXL_HUNET_WEIGHTS,23685056,sha);if(rc)return failure(c,rc);if(memcmp(sha,vxl_hunet_weights_sha,32))return -EACCES;
 stamp=vxl_hunet_mgmt_now(c);rc=vxl_hunet_io_bytes(c,VXL_HUNET_AUX,(void *)vxl_hunet_catalog_aux(),10048,1);if(rc){vxl_hunet_mgmt_mark(c,VXL_MGMT_AUX_RESTORE,stamp,10048);return failure(c,rc);}
 rc=vxl_hunet_hash_gpu(c,VXL_HUNET_AUX,10048,sha);vxl_hunet_mgmt_mark(c,VXL_MGMT_AUX_RESTORE,stamp,10048);if(rc)return failure(c,rc);if(memcmp(sha,vxl_hunet_aux_sha,32))return -EIO;
 memcpy(c->handles,handles,sizeof(handles));c->sealed=1;c->program_sealed=1;return 0;
}
int vxl_hunet_seal(struct vxl_hunet_ctx *c,const struct vxl_hunet_seal_v1 *s){return vxl_hunet_seal_mode(c,s,6,c->release->binding_sha);}
int vxl_hunet_begin(struct vxl_hunet_ctx *c,const struct vxl_hunet_begin_v1 *b,const struct vxl_hunet_io *io){
 vxl_u8 sha[32],key[32],zero[32]={0};vxl_u64 a,n;vxl_u32 poison=0x7fc00001;unsigned i;int rc;
 rc=vxl_hunet_payload_validate(VXL_HUNET_BEGIN_ROI,b,128);if(rc)return rc;
 if(!c->sealed||c->active||c->engine->job.state!=VXL_READY)return -EBUSY;
 if(c->seal_owner!=c->engine->owner||c->seal_generation!=c->engine->generation)return -ESTALE;
 if(!io||!c->input_snapshot||b->input_bytes!=196608||!b->input_ptr||b->input_ptr>~(vxl_u64)0-196608||b->roi_index>=127||!memcmp(vxl_hunet_input_sha[b->roi_index],zero,32))return -EINVAL;
 if(b->sample_kind<1||b->sample_kind>4||(b->sample_kind==1&&b->sample_index)||(b->sample_kind==2&&b->sample_index>2)||(b->sample_kind==3&&b->sample_index>19)||b->sample_index>255)return -EINVAL;
 rc=vxl_hunet_digest_hash(c,b->run_id,28,key);if(rc)return failure(c,rc); /* adjacent ROI/kind/index bytes are fixed by ABI */
 for(i=0;i<c->seen_count;i++)if(!memcmp(key,c->seen[i],32))return -EALREADY;
 if(c->seen_count==256)return -ENOSPC;
 rc=c->engine->ops->read_user(c->engine->context,c->input_snapshot,b->input_ptr,196608);if(rc)return rc;
 rc=vxl_hunet_digest_hash(c,c->input_snapshot,196608,sha);if(rc)return failure(c,rc);
 if(memcmp(sha,b->input_sha256,32)||memcmp(sha,vxl_hunet_input_sha[b->roi_index],32))return -EACCES;
 c->io=*io;
 for(i=0;i<4096;i+=4)memcpy(c->engine->scratch+i,&poison,4);
 for(a=VXL_HUNET_WS;a<VXL_HUNET_WS+VXL_HUNET_WS_BYTES;a+=n){n=VXL_HUNET_WS+VXL_HUNET_WS_BYTES-a;if(n>4096)n=4096;rc=vxl_hunet_io_bytes(c,a,c->engine->scratch,n,1);if(rc)return failure(c,rc);}
 rc=vxl_hunet_io_bytes(c,VXL_HUNET_INPUT,c->input_snapshot,196608,1);if(rc)return failure(c,rc);
 rc=vxl_hunet_io_bytes(c,VXL_HUNET_AUX,(void *)vxl_hunet_catalog_aux(),10048,1);if(rc)return failure(c,rc);
 if(c->program_id==9){rc=vxl_fused_restore_descriptors(c);if(rc)return failure(c,rc);}
 if(c->program_id==10){rc=vxl_opt_restore_descriptors(c);if(rc)return failure(c,rc);}
 rc=vxl_hunet_guards(c,1);if(rc)return failure(c,rc);
 memcpy(c->input_sha,sha,32);memcpy(c->run_id,b->run_id,16);c->roi_index=b->roi_index;c->sample_kind=b->sample_kind;c->sample_index=b->sample_index;c->next_sequence=1;c->active=1;memcpy(c->seen[c->seen_count++],key,32);if(c->program_id==8)vxl_hybrid_start_roi(c);if(c->program_id==9)return failure(c,vxl_fused_begin(c));if(c->program_id==10)return failure(c,vxl_opt_begin(c));return 0;
}
int vxl_hunet_owner_trace(struct vxl_hunet_ctx *c,vxl_u32 tiles,vxl_u32 counts[4]){
 vxl_u32 active=(tiles+3)/4,expected[4],owner,offset=0;unsigned i,j;int rc;vxl_u64 base=c->program_id==9?0x86d12e40:(c->program_id==7?0x88003c00:0x87044080);
 if(!tiles||tiles>32768)return -EINVAL;
 if(active>4)active=4;
 memset(counts,0,16);for(i=0;i<4;i++)expected[i]=i<active?tiles/active+(i<tiles%active):0;
 for(i=0;i<4;i++)for(j=0;j<expected[i];j++){rc=vxl_hunet_io_bytes(c,base+(vxl_u64)offset++*4,&owner,4,0);if(rc)return rc;if(owner!=i+1)return -EIO;counts[i]++;}
 return 0;
}
int vxl_hunet_end(struct vxl_hunet_ctx *c,struct vxl_hunet_end_v1 *out){
 vxl_u8 sha[32],logits[32];vxl_u32 words[256],counts[4];vxl_u64 a,n;unsigned i;int rc;
 if(!c->active||c->next_sequence!=1024||c->engine->job.state!=VXL_READY||c->engine->job.pinned||c->engine->quarantined)return -EBUSY;
 if(c->end_pending){*out=c->end_receipt;return 0;}
 rc=vxl_hunet_guards(c,0);if(rc)return failure(c,rc);
 rc=vxl_hunet_hash_gpu(c,VXL_HUNET_INPUT,196608,sha);if(rc)return failure(c,rc);if(memcmp(sha,c->input_sha,32))return failure(c,-EIO);
 for(a=VXL_HUNET_LOGITS;a<VXL_HUNET_LOGITS+32420;a+=n){n=VXL_HUNET_LOGITS+32420-a;if(n>sizeof(words))n=sizeof(words);rc=vxl_hunet_io_bytes(c,a,words,n,0);if(rc)return failure(c,rc);for(i=0;i<n/4;i++)if((words[i]&0x7f800000)==0x7f800000)return failure(c,-EIO);}
 rc=vxl_hunet_hash_gpu(c,VXL_HUNET_LOGITS,32420,logits);if(rc)return failure(c,rc);
 if(c->program_id==10)rc=vxl_opt_owner_trace(c,&c->opt.pending,counts);else if(c->program_id==9)rc=vxl_fused_owner_trace(c,&c->fused.pending,counts);else rc=vxl_hunet_owner_trace(c,1014,counts);if(rc)return failure(c,rc);
 if(!(c->evidence.valid_flags&VXL_HUNET_VALID_CRT)||!c->evidence.crt_guard_flags)return failure(c,-EIO);
 for(i=0;i<4;i++)if(c->evidence.exitcode[i])return failure(c,-EIO);
 memset(out,0,sizeof(*out));out->magic=VXL_HUNET_END_MAGIC;out->version=1;out->bytes=128;memcpy(out->run_id,c->run_id,16);out->roi_index=c->roi_index;out->completed_jobs=1023;out->generation=c->engine->generation;out->check_flags=31;out->last_job_id=c->engine->next_job;out->last_hw_jobs=c->evidence.jobs_after;out->final_idle=1;memcpy(out->input_sha256,sha,32);memcpy(out->logits_sha256,logits,32);c->end_receipt=*out;c->end_pending=1;return 0;
}
void vxl_hunet_delivery_capture(struct vxl_hunet_ctx *c,vxl_u32 cmd,struct vxl_hunet_delivery *d){
 memset(d,0,sizeof(*d));if(!c)return;
 if(cmd!=VXL_HUNET_GET_EVIDENCE&&cmd!=VXL_HUNET_END_ROI)return;
 d->owner=c->engine->owner;d->generation=c->engine->generation;d->command=cmd;
 if(cmd==VXL_HUNET_GET_EVIDENCE)d->payload.evidence=c->evidence;
 else if(c->end_pending)d->payload.end=c->end_receipt;else d->command=0;
}
void vxl_hunet_delivery_finish(struct vxl_hunet_ctx *c,const struct vxl_hunet_delivery *d){
 if(!c||!d->command||!c->engine->opened||d->owner!=c->engine->owner||d->generation!=c->engine->generation)return;
 if(d->command==VXL_HUNET_GET_EVIDENCE)vxl_hunet_evidence_delivered(c,&d->payload.evidence);
 else if(d->command==VXL_HUNET_END_ROI&&c->end_pending&&!memcmp(&d->payload.end,&c->end_receipt,128)){c->active=0;c->end_pending=0;}
}
int vxl_hunet_stage_args(struct vxl_hunet_ctx *c,const void *p,size_t n){if(!c||!p||n!=128||c->engine->job.state!=VXL_READY)return -EINVAL;memcpy(c->args_shadow,p,128);return 0;}
int vxl_hunet_command(struct vxl_hunet_ctx *c,vxl_u32 cmd,struct vxl_request *r){
 union {struct vxl_hunet_seal_v1 seal;struct vxl_hunet_begin_v1 begin;struct vxl_hunet_end_v1 end;struct vxl_hunet_binding_v1 binding;} p;int rc;
 if(!c||!r->user_ptr||r->user_ptr>~(vxl_u64)0-r->length)return -EFAULT;
 if(cmd==VXL_HUNET_GET_EVIDENCE){struct vxl_hunet_job_evidence_v1 ev;if(r->length!=256)return -EINVAL;rc=vxl_hunet_get_evidence(c,r->job_id,&ev);return rc?rc:c->engine->ops->write_user(c->engine->context,r->user_ptr,&ev,256);}
 if(cmd==VXL_HUNET_GET_BINDING){if(r->length!=256)return -EINVAL;p.binding=c->release->binding;p.binding.generation=c->engine->generation;p.binding.state=c->engine->job.state;return c->engine->ops->write_user(c->engine->context,r->user_ptr,&p.binding,256);}
 if(r->length!=128)return -EINVAL;
 if(cmd==VXL_HUNET_END_ROI){if(vxl_hybrid_mode(c))return -EPROTONOSUPPORT;rc=vxl_hunet_end(c,&p.end);return rc?rc:c->engine->ops->write_user(c->engine->context,r->user_ptr,&p.end,128);}
 rc=c->engine->ops->read_user(c->engine->context,&p,r->user_ptr,128);if(rc)return rc;
 if(cmd==VXL_HUNET_SEAL)return vxl_hunet_seal(c,&p.seal);
 if(cmd==VXL_HUNET_BEGIN_ROI)return vxl_hunet_begin(c,&p.begin,&c->io);
 return -ENOTTY;
}
