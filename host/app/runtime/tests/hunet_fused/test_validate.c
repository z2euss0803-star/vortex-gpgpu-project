#include <vxl/hunet.h>
#include <vxl/hunet_fused.h>
#include <assert.h>
#include <stdio.h>
#include "../../generated/hunet_fused/catalog.h"
int main(void){
 struct vxl_hunet_ctx c={0};struct vxl_fused_meta meta;vxl_u32 words[32];unsigned i,j;
 c.program_id=9;
 for(i=0;i<103;i++){
  memcpy(words,vxl_fused_jobs[i],128);
  assert(!vxl_fused_validate_job(&c,words,vxl_fused_metadata[i].first,&meta));
  assert(!memcmp(&meta,&vxl_fused_metadata[i],sizeof(meta)));
  assert(vxl_fused_validate_job(&c,words,meta.first+1,&meta));
  for(j=0;j<32;j++){words[j]^=1;assert(vxl_fused_validate_job(&c,words,vxl_fused_metadata[i].first,&meta));words[j]^=1;}
 }
 memcpy(words,vxl_fused_jobs[0],128);c.program_id=8;
 assert(vxl_fused_validate_job(&c,words,1,&meta));
 assert(vxl_hunet_validate_job(&c,(const struct vxl_hunet_job_v1*)words,1));
 c.program_id=9;memcpy(words,vxl_hunet_catalog_job(1),128);
 assert(vxl_fused_validate_job(&c,words,1,&meta));
 memcpy(words,vxl_hunet_catalog_job(296),128);
 assert(vxl_fused_validate_job(&c,words,296,&meta));
 memcpy(words,vxl_fused_jobs[0],128);{vxl_u64 va=0x00007fff85000040ULL;memcpy(words+8,&va,8);}
 assert(vxl_fused_validate_job(&c,words,1,&meta));
 memcpy(words,vxl_fused_jobs[0],128);{vxl_u64 pl=0x506c4df40ULL;memcpy(words+12,&pl,8);}
 assert(vxl_fused_validate_job(&c,words,1,&meta));
 memcpy(words,vxl_fused_jobs[0],128);words[14]=words[12];
 assert(vxl_fused_validate_job(&c,words,1,&meta));
 assert(vxl_fused_decode_meta(words,127,&meta));assert(vxl_fused_decode_meta(NULL,128,&meta));
 puts("FUSED_EXACT_103_CATALOG_MUTATION_REJECTION_PASS_NOT_GPU");
}
