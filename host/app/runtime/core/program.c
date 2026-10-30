#include <vxl/core.h>
#include <vxl/cnn.h>
#include "../generated/cnn/programs.h"
/* Exact r4 diagnostic payload; absolute common_entry=0x10000010. */
static const vxl_u32 words[]={
 0x00400293,0x10000337,0x01030313,0x0062900b,0x00f00293,0x0002800b,0x340023f3,0xf1402473,
 0x00241413,0x0003a483,0x0043a503,0x0083a583,0x008484b3,0x00850533,0x008585b3,0x0004a603,
 0x00052683,0x00d60733,0x00e5a023,0x0330000f,0x0000000b,0x0000006f
};
int vxl_program_check(const vxl_u8 *bytes,vxl_u64 n,vxl_u32 gpu){
 vxl_u64 i;
 if(!bytes||gpu!=0x10000000||n<sizeof(words)||n>4096)return -EACCES;
 if(memcmp(bytes,words,sizeof(words)))return -EACCES;
 for(i=sizeof(words);i<n;i++)if(bytes[i])return -EACCES;
 return 0;
}
int vxl_program_check_id(const vxl_u8 *bytes,vxl_u64 n,vxl_u32 gpu,vxl_u32 id){
 const vxl_u32 *frozen;
 if(id==1)return vxl_program_check(bytes,n,gpu);
 if(!bytes||n!=VXL_CNN_CODE_BYTES||gpu!=VXL_CNN_CODE_GPU)return -EACCES;
 switch(id){
 case VXL_CNN_CONV:frozen=vxl_cnn_program_2;break;
 case VXL_CNN_RELU:frozen=vxl_cnn_program_3;break;
 case VXL_CNN_POOL:frozen=vxl_cnn_program_4;break;
 case VXL_CNN_FC:frozen=vxl_cnn_program_5;break;
 default:return -EACCES;
 }
 return memcmp(bytes,frozen,VXL_CNN_CODE_BYTES)?-EACCES:0;
}
