#include <vxl/engine.h>
#include <vxl/hunet.h>
#include <vxl/hunet_fused.h>
#include "../../generated/hunet_fused/layout.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static int rw(void *context,vxl_u64 address,void *bytes,vxl_u64 n,int write){
 unsigned char *memory=context;
 if(address<VXL_HUNET_WS||vxl_range_check(VXL_HUNET_WS_BYTES,address-VXL_HUNET_WS,n))return -ERANGE;
 if(write)memcpy(memory+address-VXL_HUNET_WS,bytes,(size_t)n);
 else memcpy(bytes,memory+address-VXL_HUNET_WS,(size_t)n);
 return 0;
}
int main(void){
 unsigned char *memory=malloc(VXL_HUNET_WS_BYTES);unsigned i;
 struct vxl_hunet_ctx c={0};assert(memory);
 c.io.context=memory;c.io.rw=rw;c.program_id=9;
 memset(memory,0x5a,VXL_HUNET_WS_BYTES);
 assert(vxl_hunet_guards(&c,1)==0);
 for(i=0;i<3;i++)assert(memory[vxl_fused_guards[i]-VXL_HUNET_WS]==0xa5);
 assert(memory[VXL_FUSED_DESCRIPTORS_START-VXL_HUNET_WS]==0x5a);
 assert(memory[VXL_FUSED_PACKED_B_START-VXL_HUNET_WS]==0x5a);
 assert(vxl_hunet_guards(&c,0)==0);
 for(i=0;i<3;i++){
  memory[vxl_fused_guards[i]-VXL_HUNET_WS+63]^=1;
  assert(vxl_hunet_guards(&c,0)==-EIO);
  memory[vxl_fused_guards[i]-VXL_HUNET_WS+63]^=1;
 }
 c.program_id=8;memset(memory,0x5a,VXL_HUNET_WS_BYTES);
 assert(vxl_hunet_guards(&c,1)==0);
 for(i=0;i<3;i++)assert(memory[vxl_fused_guards[i]-VXL_HUNET_WS]==0x5a);
 assert(vxl_hunet_guards(&c,0)==0);
 free(memory);puts("FUSED_INTERNAL_GUARDS_AND_OLD_MODE_PRESERVED_PASS_NOT_BOARD");return 0;
}
