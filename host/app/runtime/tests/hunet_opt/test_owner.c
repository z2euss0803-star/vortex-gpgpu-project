#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <vxl/hunet_opt.h>
int main(int argc,char **argv){
 const struct vxl_opt_catalog *cat=vxl_opt_catalog_default();FILE *f;unsigned jobs=0;vxl_u64 tiles=0;
 assert(argc==2);f=fopen(argv[1],"rb");assert(f);
 for(unsigned i=0;i<cat->gpu_jobs;i++){
  const struct vxl_opt_meta *m=&cat->metadata[i];if(!m->owner_tiles)continue;jobs++;
  for(unsigned tile=0;tile<m->owner_tiles;tile++){
   vxl_u32 expected,actual;assert(fread(&expected,4,1,f)==1);assert(!vxl_opt_owner_expected(cat,m,tile,&actual));assert(actual==expected);tiles++;
  }
  vxl_u32 bad;assert(vxl_opt_owner_expected(cat,m,m->owner_tiles,&bad));
 }
 assert(jobs&&tiles&&fgetc(f)==EOF);fclose(f);puts("HOST_OWNER_MAPPING_MATCHES_INDEPENDENT_GROUP_SCHEDULE_NOT_GPU");return 0;
}
