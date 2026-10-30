#include <vxl/v2/provider.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
 FILE*f;long n;vxl_u8*raw;struct vxl_v2_catalog catalog;struct vxl_v2_catalog_entry entry;struct vxl_v2_boot_identity hw={0};
 assert(argc==2);f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));n=ftell(f);assert(n>=64&&n<=65536);rewind(f);raw=malloc((size_t)n);assert(raw&&fread(raw,1,(size_t)n,f)==(size_t)n);fclose(f);
 memcpy(&entry,raw+64,512);hw.provider.flags=4;memcpy(hw.provider.hardware_contract,entry.hardware_contract,32);memcpy(hw.provider.bit,entry.bit,32);memcpy(hw.provider.xsa,entry.xsa,32);strcpy(hw.compatible,"vortex,hw4-s8-joint-v1");
 assert(vxl_v2_catalog_open(&catalog,raw,(vxl_u32)n,&hw.provider)==0&&catalog.count==16);
 {unsigned nn=0,image=0,p8=0;for(unsigned i=0;i<catalog.count;i++){
   memcpy(&entry,raw+64+512*i,512);assert(vxl_v2_s8_admit(&hw,&entry)==0);assert(vxl_v2_p8_admit(&hw,&entry)==-EXDEV);assert(entry.logical_program!=65537);
   nn+=entry.namespace_id==1;image+=entry.namespace_id==3;p8+=entry.namespace_id==2;
  }assert(nn==2&&image==10&&p8==4);
 }
 hw.provider.flags=1;assert(vxl_v2_catalog_open(&catalog,raw,(vxl_u32)n,&hw.provider)==-EXDEV);hw.provider.flags=2;assert(vxl_v2_catalog_open(&catalog,raw,(vxl_u32)n,&hw.provider)==-EXDEV);hw.provider.flags=4;
 assert(vxl_v2_catalog_open(&catalog,raw,65537,&hw.provider)==-EINVAL);raw[64]^=1;assert(vxl_v2_catalog_open(&catalog,raw,(vxl_u32)n,&hw.provider)==-EBADMSG);
 free(raw);puts("S8_JOINT_ACTUAL_C_CATALOG_16_PROGRAMS_SCHEMAS_IDENTITY_AND_OLD_REJECTION_PASS_NOT_BOARD");return 0;
}
