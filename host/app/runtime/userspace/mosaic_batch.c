#include <vxl/mosaic_client.h>
int mosaic_batch_next(vxl_u32 items,vxl_u32 first,vxl_u32 limit,vxl_u32 *count){
 if(!count||!items||first>=items||!limit||limit>4096)return -EINVAL;
 *count=items-first;if(*count>limit)*count=limit;return 0;
}
int mosaic_client_set_batch_policy(struct mosaic_client *c,const vxl_u32 limits[6]){
 if(!c||!limits)return -EINVAL;
 if(c->frame_active)return -EBUSY;
 if(c->first_error)return c->first_error;
 for(unsigned i=0;i<6;++i)if(!limits[i]||limits[i]>4096)return -ERANGE;
 memcpy(c->batch_limits,limits,sizeof(c->batch_limits));return 0;
}
int mosaic_client_set_prefix_budget(struct mosaic_client *c,vxl_u32 jobs,vxl_u64 bytes){
 if(!c)return -EINVAL;
 if(c->frame_active)return -EBUSY;
 if(c->first_error)return c->first_error;
 if(!jobs||jobs>8||bytes<512||bytes>8*512)return -ERANGE;
 c->prefix_max_jobs=jobs;c->prefix_max_evidence_bytes=bytes;return 0;
}
int mosaic_client_set_frame_budget(struct mosaic_client *c,vxl_u32 jobs,vxl_u64 bytes){
 if(!c)return -EINVAL;
 if(c->frame_active)return -EBUSY;
 if(c->first_error)return c->first_error;
 if(!jobs||jobs==0xffffffffu||bytes<512)return -ERANGE;
 c->frame_max_jobs=jobs;c->frame_max_evidence_bytes=bytes;return 0;
}
