#include <vxl/engine.h>
#include <vxl/hunet.h>
#include "../generated/hunet/programs.h"
#include "../generated/hunet/assets.h"
#ifdef VXL_HUNET_SOURCE_BOUND
#include <vxl_hunet_source.h>
#endif
void vxl_hunet_release_defaults(struct vxl_hunet_release *r){
 memset(r,0,sizeof(*r));r->program[0].id=6;r->program[0].wire_bytes=VXL_HUNET6_WIRE_BYTES;r->program[0].ro_bytes=VXL_HUNET6_RO0_BYTES;
 memcpy(r->program[0].wire_sha,vxl_hunet6_wire_sha256,32);memcpy(r->program[0].ro_sha,vxl_hunet6_ro0_sha256,32);
 r->program[1].id=7;r->program[1].wire_bytes=VXL_HUNET7_WIRE_BYTES;r->program[1].ro_bytes=VXL_HUNET7_RO0_BYTES;
 memcpy(r->program[1].wire_sha,vxl_hunet7_wire_sha256,32);memcpy(r->program[1].ro_sha,vxl_hunet7_ro0_sha256,32);
 r->binding.magic=VXL_HUNET_BINDING_MAGIC;r->binding.version=1;r->binding.bytes=256;r->binding.profile_id=4;r->binding.program_id=6;r->binding.features=3;
 memcpy(r->binding.profile_sha256,vxl_hunet_profile_sha,32);memcpy(r->binding.program_sha256,vxl_hunet6_wire_sha256,32);memcpy(r->binding.weights_sha256,vxl_hunet_weights_sha,32);memcpy(r->binding.jobs_sha256,vxl_hunet_jobs_sha,32);memcpy(r->binding.aux_sha256,vxl_hunet_aux_sha,32);memcpy(r->binding.model_sha256,vxl_hunet_model_sha,32);
#ifdef VXL_HUNET_SOURCE_BOUND
 {unsigned i;for(i=0;i<32;i++)r->binding.source_sha256[i]=vxl_hunet_source_sha[i];}
#endif
 vxl_hunet_hash(r->binding.profile_sha256,224,r->binding_sha);
}
int vxl_hunet_hash_gpu(struct vxl_hunet_ctx *c,vxl_u64 a,vxl_u64 n,vxl_u8 out[32]){
 struct vxl_hunet_digest_ctx s;vxl_u64 take,stamp;int rc;
 int code=a>=VXL_HUNET_CODE&&a<VXL_HUNET_CODE+8388608;
 int weights=a>=VXL_HUNET_WEIGHTS&&a<VXL_HUNET_WEIGHTS+23685056;
 int timed=c->mgmt_row&&(c->mgmt_row->key.command==VXL_HUNET_SEAL||c->mgmt_row->key.command==VXL_HYBRID_SEAL)&&(code||weights);
 enum vxl_mgmt_field read_field=code?VXL_MGMT_SEAL_CODE_READ:VXL_MGMT_SEAL_WEIGHTS_READ,hash_field=code?VXL_MGMT_SEAL_CODE_HASH:VXL_MGMT_SEAL_WEIGHTS_HASH;
 stamp=timed?vxl_hunet_mgmt_now(c):0;rc=vxl_hunet_digest_init(c,&s);if(timed)vxl_hunet_mgmt_mark(c,hash_field,stamp,0);if(rc)return rc;
 while(n){take=n>4096?4096:n;stamp=timed?vxl_hunet_mgmt_now(c):0;rc=vxl_hunet_io_bytes(c,a,c->engine->scratch,take,0);if(timed)vxl_hunet_mgmt_mark(c,read_field,stamp,take);if(rc){vxl_hunet_digest_abort(&s);return rc;}
  stamp=timed?vxl_hunet_mgmt_now(c):0;rc=vxl_hunet_digest_update(&s,c->engine->scratch,take);if(timed)vxl_hunet_mgmt_mark(c,hash_field,stamp,take);if(rc){vxl_hunet_digest_abort(&s);return rc;}a+=take;n-=take;
 }
 stamp=timed?vxl_hunet_mgmt_now(c):0;rc=vxl_hunet_digest_final(&s,out);if(timed)vxl_hunet_mgmt_mark(c,hash_field,stamp,0);vxl_hunet_digest_abort(&s);return rc;
}
int vxl_hunet_check_program(struct vxl_hunet_ctx *c,vxl_u32 initial){
 const struct vxl_hunet_program *p;vxl_u8 sha[32];int rc;
 if(c&&c->program_id==10)return vxl_opt_check_program(c);
 if(c&&c->program_id==9){(void)initial;return vxl_fused_check_program(c);}
 if(!c||!c->release||(c->program_id!=6&&c->program_id!=7&&c->program_id!=8))return -EACCES;
 if(!initial&&(c->seal_owner!=c->engine->owner||c->seal_generation!=c->engine->generation))return -ESTALE;
 /* Program8 keeps program6 wire and CRT; its session contract is separate. */
 p=&c->release->program[c->program_id==8?0:c->program_id-6];
 rc=vxl_hunet_hash_gpu(c,VXL_HUNET_CODE,initial?p->wire_bytes:p->ro_bytes,sha);if(rc)return rc;
 if(memcmp(sha,initial?p->wire_sha:p->ro_sha,32))return -EACCES;
 if(initial){c->seal_owner=c->engine->owner;c->seal_generation=c->engine->generation;}return 0;
}
