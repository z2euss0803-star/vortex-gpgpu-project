#include <vxl/engine.h>
#include <vxl/hunet.h>
#include "../generated/hunet_fused/identity.h"
int vxl_fused_init(struct vxl_hunet_ctx *c,const struct vxl_fused_release *r){
 const struct vxl_hunet_binding_v1 *b,*old;vxl_u8 sha[32];int rc;
 if(!c||!r||!c->release||c->sealed||c->active)return -EINVAL;
 rc=vxl_fused_payload_validate(VXL_HYBRID_GET_BINDING,&r->binding,512);if(rc)return rc;
 b=&r->binding.v2.v1;old=&c->release->binding;
 if(b->generation||b->state||!r->wire_bytes||r->wire_bytes>8388608||!r->ro_bytes||r->ro_bytes>r->wire_bytes||
    memcmp(b->profile_sha256,old->profile_sha256,32)||memcmp(b->model_sha256,old->model_sha256,32)||
    memcmp(b->weights_sha256,old->weights_sha256,32)||memcmp(b->aux_sha256,old->aux_sha256,32)||
    memcmp(b->program_sha256,r->wire_sha,32)||memcmp(b->jobs_sha256,vxl_fused_jobs_sha256,32)||
    memcmp(r->binding.fusion_contract_sha256,vxl_fused_contract_sha256,32)||
    memcmp(r->binding.descriptors_sha256,vxl_fused_descriptors_sha256,32)||
    memcmp(r->binding.layout_sha256,vxl_fused_layout_sha256,32))return -ESTALE;
 if(c->hybrid.release&&(memcmp(r->binding.v2.cpu_contract_sha256,c->hybrid.release->binding.cpu_contract_sha256,32)||
    memcmp(r->binding.v2.cpu_stages_sha256,c->hybrid.release->binding.cpu_stages_sha256,32)))return -ESTALE;
 rc=vxl_hunet_digest_hash(c,&r->binding,512,sha);if(rc)return rc;if(memcmp(sha,r->binding_sha,32))return -ESTALE;
 c->fused.release=r;return 0;
}
int vxl_fused_seal(struct vxl_hunet_ctx *c,const struct vxl_hunet_seal_v1 *s){
 struct vxl_hunet_seal_v1 old;int rc;
 if(!c||!c->fused.release)return -EOPNOTSUPP;
 rc=vxl_hybrid_payload_validate(VXL_HYBRID_SEAL,s,128);if(rc)return rc;
 if(memcmp(s->binding_sha256,c->fused.release->binding_sha,32))return -ESTALE;
 old=*s;old.magic=VXL_HUNET_SEAL_MAGIC;
 rc=vxl_hunet_seal_mode(c,&old,9,c->fused.release->binding_sha);if(rc)return rc;
 c->hybrid.enabled=1;c->hybrid.owner=c->engine->owner;c->hybrid.generation=c->engine->generation;return 0;
}
int vxl_fused_begin(struct vxl_hunet_ctx *c){
 if(!c||c->program_id!=9||!c->fused.release||!c->sealed||!c->active||!c->hybrid.enabled)return -EACCES;
 if(!c->engine->opened||c->engine->quarantined||c->engine->job.state!=VXL_READY||c->engine->job.pinned)return -EIO;
 if(c->seal_owner!=c->engine->owner||c->seal_generation!=c->engine->generation)return -ESTALE;
 memset(&c->fused.pending,0,sizeof(c->fused.pending));c->fused.acked=0;
 vxl_hybrid_start_roi(c);return 0;
}
int vxl_fused_gpu_completed(struct vxl_hunet_ctx *c,const struct vxl_fused_meta *m){
 struct vxl_fused_meta expected;const vxl_u32*j;vxl_u32 required=VXL_HUNET_VALID_CRT|VXL_HUNET_VALID_TIMING|VXL_HUNET_VALID_HW_SNAPSHOT|VXL_HUNET_VALID_EXIT_SNAPSHOT;
 if(!c||!m||c->program_id!=9||!c->fused.release||!c->sealed||!c->active||!c->hybrid.enabled)return -EACCES;
 if(!c->engine->opened||c->hybrid.owner!=c->engine->owner||c->hybrid.generation!=c->engine->generation)return -ESTALE;
 vxl_hybrid_poll(c);if(c->engine->quarantined)return -EIO;
 if(vxl_hybrid_busy(c)||!c->fused.acked||!c->evidence_delivered)return -EBUSY;
 j=vxl_fused_catalog_job(c->next_sequence);if(!j)return -ESTALE;
 if(vxl_fused_decode_meta(j,128,&expected)||memcmp(m,&expected,sizeof(expected))||memcmp(m,&c->fused.pending,sizeof(*m)))return -ESTALE;
 if(c->evidence.result||c->evidence.software_state!=VXL_COMPLETED||(c->evidence.valid_flags&required)!=required||
    c->evidence.generation!=c->engine->generation||c->evidence.sequence!=m->first||c->evidence.stage_id!=m->stage||c->evidence.op!=m->op)return -EIO;
 if(m->op==15&&!(c->evidence.valid_flags&VXL_HUNET_VALID_OWNER))return -EIO;
 c->next_sequence=m->last+1;c->hybrid.logical_jobs+=m->last-m->first+1;c->hybrid.gpu_jobs++;c->fused.acked=0;
 return 0;
}
