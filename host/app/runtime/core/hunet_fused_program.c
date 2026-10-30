#include <vxl/hunet.h>
#include <vxl/engine.h>
#include "../generated/hunet_fused/descriptors.h"
#include "../generated/hunet_fused/layout.h"
int vxl_fused_check_program(struct vxl_hunet_ctx *c){
 const struct vxl_fused_release *r;vxl_u8 sha[32],expected[32];int rc;
 if(!c||c->program_id!=9||!c->fused.release||!c->engine)return -EACCES;
 r=c->fused.release;
 if(!r->wire_bytes||r->wire_bytes>8388608||!r->ro_bytes||r->ro_bytes>r->wire_bytes)return -EINVAL;
 if(c->program_sealed&&(c->seal_owner!=c->engine->owner||c->seal_generation!=c->engine->generation))return -ESTALE;
 rc=vxl_hunet_hash_gpu(c,VXL_HUNET_CODE,c->program_sealed?r->ro_bytes:r->wire_bytes,sha);if(rc)return rc;
 if(memcmp(sha,c->program_sealed?r->ro_sha:r->wire_sha,32))return -EACCES;
 rc=vxl_hunet_digest_hash(c,vxl_fused_descriptors,sizeof(vxl_fused_descriptors),expected);if(rc)return rc;
 if(memcmp(expected,r->binding.descriptors_sha256,32))return -ESTALE;
 rc=vxl_hunet_hash_gpu(c,VXL_FUSED_DESCRIPTORS_START,VXL_FUSED_DESCRIPTORS_BYTES,sha);if(rc)return rc;
 if(memcmp(sha,expected,32))return -EACCES;
 if(!c->program_sealed){c->seal_owner=c->engine->owner;c->seal_generation=c->engine->generation;}
 return 0;
}
int vxl_fused_restore_descriptors(struct vxl_hunet_ctx *c){
 vxl_u8 sha[32],expected[32];int rc;
 if(!c||c->program_id!=9||!c->fused.release)return -EACCES;
 rc=vxl_hunet_digest_hash(c,vxl_fused_descriptors,sizeof(vxl_fused_descriptors),expected);if(rc)return rc;
 if(memcmp(expected,c->fused.release->binding.descriptors_sha256,32))return -ESTALE;
 rc=vxl_hunet_io_bytes(c,VXL_FUSED_DESCRIPTORS_START,(void *)vxl_fused_descriptors,sizeof(vxl_fused_descriptors),1);if(rc)return rc;
 rc=vxl_hunet_hash_gpu(c,VXL_FUSED_DESCRIPTORS_START,sizeof(vxl_fused_descriptors),sha);if(rc)return rc;
 return memcmp(sha,expected,32)?-EIO:0;
}
