#include <vxl/hunet.h>
#include "../generated/hunet_fused/layout.h"
int vxl_fused_owner_trace(struct vxl_hunet_ctx *c,const struct vxl_fused_meta *m,vxl_u32 counts[4]){
 struct vxl_fused_meta expected;const vxl_u32*j;vxl_u32 words[256],active,core=0,remaining,i,offset=0,take;int rc;
 if(!c||!m||!counts||c->program_id!=9||m->op!=15)return -EINVAL;
 j=vxl_fused_catalog_job(m->first);if(!j)return -ESTALE;
 rc=vxl_fused_decode_meta(j,128,&expected);if(rc)return rc;
 if(memcmp(m,&expected,sizeof(expected))||!m->trace_elements||m->trace_elements>VXL_FUSED_OWNER_TRACE_BYTES/4)return -ESTALE;
 active=(m->trace_elements+3)/4;if(active>4)active=4;
 memset(counts,0,16);remaining=m->trace_elements/active+(m->trace_elements%active>0);
 while(offset<m->trace_elements){
  take=m->trace_elements-offset;if(take>256)take=256;
  rc=vxl_hunet_io_bytes(c,VXL_FUSED_OWNER_TRACE_START+(vxl_u64)offset*4,words,(vxl_u64)take*4,0);if(rc)return rc;
  for(i=0;i<take;i++){
   if(words[i]!=core+1)return -EIO;
   counts[core]++;
   if(!--remaining){core++;if(core<active)remaining=m->trace_elements/active+(core<m->trace_elements%active);}
  }
  offset+=take;
 }
 return 0;
}
