#include <vxl/engine.h>
#include <vxl/hunet.h>
int vxl_opt_seal(struct vxl_hunet_ctx *c,const struct vxl_hunet_seal_v1 *s){
 struct vxl_hunet_seal_v1 old;int rc;
 if(!c||!c->opt.release)return -EOPNOTSUPP;
 rc=vxl_hybrid_payload_validate(VXL_HYBRID_SEAL,s,128);if(rc)return rc;
 if(memcmp(s->binding_sha256,c->opt.release->binding_sha,32))return -ESTALE;
 old=*s;old.magic=VXL_HUNET_SEAL_MAGIC;
 rc=vxl_hunet_seal_mode(c,&old,10,c->opt.release->binding_sha);if(rc)return rc;
 c->hybrid.enabled=1;c->hybrid.owner=c->engine->owner;c->hybrid.generation=c->engine->generation;return 0;
}
int vxl_opt_begin(struct vxl_hunet_ctx *c){
 if(!c||c->program_id!=10||!c->opt.release||!c->sealed||!c->active||!c->hybrid.enabled)return -EACCES;
 if(!c->engine->opened||c->engine->quarantined||c->engine->job.state!=VXL_READY||c->engine->job.pinned)return -EIO;
 if(c->seal_owner!=c->engine->owner||c->seal_generation!=c->engine->generation)return -ESTALE;
 memset(&c->opt.pending,0,sizeof(c->opt.pending));c->opt.acked=0;vxl_hybrid_start_roi(c);return 0;
}
int vxl_opt_gpu_completed(struct vxl_hunet_ctx *c,const struct vxl_opt_meta *m){
 struct vxl_opt_meta expected;const vxl_u32 *job;vxl_u32 required=27u;int rc;
 if(!c||!m||c->program_id!=10||!c->opt.release||!c->sealed||!c->active||!c->hybrid.enabled)return -EACCES;
 if(!c->engine->opened||c->hybrid.owner!=c->engine->owner||c->hybrid.generation!=c->engine->generation)return -ESTALE;
 vxl_hybrid_poll(c);if(c->engine->quarantined)return -EIO;
 if(vxl_hybrid_busy(c)||!c->opt.acked||!c->evidence_delivered)return -EBUSY;
 job=vxl_opt_catalog_job(c->opt.release->catalog,c->next_sequence);if(!job)return -ESTALE;
 rc=vxl_opt_validate_job(c,job,c->next_sequence,&expected);if(rc)return rc;
 if(memcmp(m,&expected,sizeof(*m))||memcmp(m,&c->opt.pending,sizeof(*m)))return -ESTALE;
 if(m->owner_tiles)required|=VXL_HUNET_VALID_OWNER;
 if(c->evidence.program_id!=10||c->evidence.result||c->evidence.software_state!=VXL_COMPLETED||(c->evidence.valid_flags&required)!=required||
    c->evidence.generation!=c->engine->generation||c->evidence.sequence!=m->first||c->evidence.stage_id!=m->first_stage||c->evidence.op!=m->op)return -EIO;
 c->next_sequence=m->last+1;c->hybrid.logical_jobs+=m->last-m->first+1;c->hybrid.gpu_jobs++;c->opt.acked=0;return 0;
}
const vxl_u32 *vxl_opt_catalog_job(const struct vxl_opt_catalog *cat,vxl_u32 first){
 unsigned i;if(!cat)return NULL;
 for(i=0;i<cat->gpu_jobs;i++)if(cat->metadata[i].first==first)return cat->jobs[i];
 return NULL;
}
int vxl_opt_check_program(struct vxl_hunet_ctx *c){
 const struct vxl_opt_release *r;vxl_u8 sha[32];int rc;
 if(!c||!c->engine||c->program_id!=10||!c->opt.release)return -EACCES;
 r=c->opt.release;
 if(c->program_sealed&&(c->seal_owner!=c->engine->owner||c->seal_generation!=c->engine->generation))return -ESTALE;
 rc=vxl_hunet_hash_gpu(c,VXL_HUNET_CODE,c->program_sealed?r->ro_bytes:r->wire_bytes,sha);if(rc)return rc;
 if(memcmp(sha,c->program_sealed?r->ro_sha:r->wire_sha,32))return -EACCES;
 if(!c->program_sealed){c->seal_owner=c->engine->owner;c->seal_generation=c->engine->generation;}
 return 0;
}
