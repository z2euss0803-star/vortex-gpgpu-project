#include <cassert>
#include <cstdio>
#include "../../device/hunet_opt_pack.h"
int main(){
 HunetFusedDescriptorV1 d[32]={};
 for(unsigned i=0;i<32;i++){d[i].m=d[i].mp=8;d[i].n=d[i].np=d[i].count=4;d[i].k=d[i].kp=8;d[i].pack_grid_base=i*8;d[i].gemm_grid_base=i;}
 assert(opt_descriptor_table_valid(d,32));
 for(unsigned i=0;i<32;i++){assert(opt_find_descriptor(d,32,i*8,true)==int(i));assert(opt_find_descriptor(d,32,i*8+7,true)==int(i));assert(opt_find_descriptor(d,32,i,false)==int(i));}
 assert(opt_find_descriptor(d,32,256,true)==-1&&opt_find_descriptor(d,32,32,false)==-1);
 assert(opt_find_descriptor(d,0,0,true)==-1&&opt_find_descriptor(nullptr,1,0,true)==-1);
 assert(opt_descriptor_table_valid(d,1)&&opt_find_descriptor(d,1,7,true)==0);
 d[1].pack_grid_base=7;assert(!opt_descriptor_table_valid(d,32));assert(opt_find_descriptor(d,32,8,true)==-1);d[1].pack_grid_base=8;
 d[1].pack_grid_base=0;assert(!opt_descriptor_table_valid(d,32));assert(opt_find_descriptor(d,32,0,true)==-1);d[1].pack_grid_base=8;
 d[31].pack_grid_base=UINT32_MAX-3;assert(!opt_descriptor_table_valid(d,32));assert(opt_find_descriptor(d,32,UINT32_MAX,true)==-1);
 puts("DESCRIPTOR_PREFIX_BINARY_LOOKUP_BOUNDARIES_OVERFLOW_PASS_NOT_GPU");
}
