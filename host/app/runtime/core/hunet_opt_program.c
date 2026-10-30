#include <vxl/hunet.h>
#include <vxl/engine.h>
int vxl_opt_init(struct vxl_hunet_ctx *c,const struct vxl_opt_release *release){
 const struct vxl_opt_catalog *cat;vxl_u8 sha[32];struct vxl_hunet_digest_ctx digest;int rc;
 if(!c||!c->release||!c->engine||c->engine->profile_id!=4||!release||!release->catalog)return -EINVAL;
 if(c->sealed||c->active)return -EBUSY;
 if(release->digest_backend!=VXL_DIGEST_AUTO&&release->digest_backend!=VXL_DIGEST_PORTABLE)return -EINVAL;
 if(!release->wire_bytes||release->wire_bytes>8388608||!release->ro_bytes||release->ro_bytes>release->wire_bytes)return -EINVAL;
 rc=vxl_opt_payload_validate(VXL_HYBRID_GET_BINDING,&release->binding,sizeof(release->binding));if(rc)return rc;
 if(release->binding.v3.v2.v1.generation||release->binding.v3.v2.v1.state||memcmp(release->binding.v3.v2.v1.program_sha256,release->wire_sha,32)||
    memcmp(release->binding.v3.v2.v1.profile_sha256,c->release->binding.profile_sha256,32)||memcmp(release->binding.v3.v2.v1.model_sha256,c->release->binding.model_sha256,32)||
    memcmp(release->binding.v3.v2.v1.weights_sha256,c->release->binding.weights_sha256,32)||memcmp(release->binding.v3.v2.v1.aux_sha256,c->release->binding.aux_sha256,32))return -ESTALE;
 if(c->hybrid.release&&(memcmp(release->binding.v3.v2.cpu_contract_sha256,c->hybrid.release->binding.cpu_contract_sha256,32)||memcmp(release->binding.v3.v2.cpu_stages_sha256,c->hybrid.release->binding.cpu_stages_sha256,32)))return -ESTALE;
 cat=release->catalog;if(release->digest_backend!=((cat->runtime_flags&1u)?VXL_DIGEST_AUTO:VXL_DIGEST_PORTABLE))return -EINVAL;if(!cat->gpu_jobs||cat->gpu_jobs>1023||cat->cpu_jobs!=13||cat->logical_jobs!=1023||!cat->jobs||!cat->metadata||!cat->cpu||!cat->matrix_data||cat->graph_bytes>64*4096u)return -EINVAL;
 if(memcmp(cat->jobs_sha,release->binding.v3.v2.v1.jobs_sha256,32)||memcmp(cat->descriptors_sha,release->binding.v3.descriptors_sha256,32)||memcmp(cat->layout_sha,release->binding.v3.layout_sha256,32))return -ESTALE;
 rc=vxl_hunet_digest_hash(c,&release->binding,sizeof(release->binding),sha);if(rc)return rc;
 if(memcmp(sha,release->binding_sha,32))return -ESTALE;
 rc=vxl_hunet_digest_hash(c,cat->jobs,(vxl_u64)cat->gpu_jobs*128,sha);if(rc)return rc;if(memcmp(sha,cat->jobs_sha,32))return -ESTALE;
 rc=vxl_hunet_digest_hash(c,cat->matrix_data,cat->matrix_bytes,sha);if(rc)return rc;if(memcmp(sha,cat->matrix_sha,32))return -ESTALE;
 if(cat->graph_bytes&&(!cat->graph_data||(cat->graph_bytes&4095u)))return -EINVAL;
 rc=vxl_hunet_digest_hash(c,cat->graph_data,cat->graph_bytes,sha);if(rc)return rc;if(memcmp(sha,cat->graph_sha,32))return -ESTALE;
 rc=vxl_hunet_digest_init(c,&digest);if(!rc)rc=vxl_hunet_digest_update(&digest,cat->matrix_data,cat->matrix_bytes);if(!rc)rc=vxl_hunet_digest_update(&digest,cat->graph_data,cat->graph_bytes);if(!rc)rc=vxl_hunet_digest_final(&digest,sha);vxl_hunet_digest_abort(&digest);if(rc)return rc;if(memcmp(sha,cat->descriptors_sha,32))return -ESTALE;
 memset(&c->opt,0,sizeof(c->opt));c->opt.release=release;return 0;
}
int vxl_opt_restore_descriptors(struct vxl_hunet_ctx *c){
 const struct vxl_opt_catalog *cat;vxl_u8 sha[32];int rc;
 if(!c||!c->engine||c->program_id!=10||!c->opt.release||!c->opt.release->catalog)return -EACCES;
 cat=c->opt.release->catalog;
 rc=vxl_hunet_io_bytes(c,0x86d0df80,(void*)cat->matrix_data,cat->matrix_bytes,1);if(rc)return rc;
 rc=vxl_hunet_hash_gpu(c,0x86d0df80,cat->matrix_bytes,sha);if(rc)return rc;if(memcmp(sha,cat->matrix_sha,32))return -EIO;
 if(cat->graph_bytes){rc=vxl_hunet_io_bytes(c,0x84000000,(void*)cat->graph_data,cat->graph_bytes,1);if(rc)return rc;rc=vxl_hunet_hash_gpu(c,0x84000000,cat->graph_bytes,sha);if(rc)return rc;if(memcmp(sha,cat->graph_sha,32))return -EIO;}
 return 0;
}
