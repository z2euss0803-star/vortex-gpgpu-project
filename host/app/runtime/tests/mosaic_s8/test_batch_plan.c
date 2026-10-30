#include <vxl/mosaic_client.h>
#include <assert.h>
#include <stdio.h>
static void coverage(vxl_u32 items,vxl_u32 limit){
 vxl_u32 first=0,seen=0;
 while(first<items){vxl_u32 count=0;assert(!mosaic_batch_next(items,first,limit,&count));assert(count&&count<=limit&&count<=items-first);assert(first==seen);seen+=count;first+=count;}
 assert(seen==items);vxl_u32 tail=99;assert(mosaic_batch_next(items,items,limit,&tail)<0);
}
int main(void){
 const vxl_u32 cases[]={1,3,4,4095,4096,4097,614400};
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i){coverage(cases[i],4);coverage(cases[i],4096);coverage(cases[i],3);}
 vxl_u32 count=0;assert(mosaic_batch_next(10,0,0,&count)<0);assert(mosaic_batch_next(10,0,4097,&count)<0);assert(mosaic_batch_next(10,0,4,NULL)<0);
 struct mosaic_client c={0};vxl_u32 policy[6]={4096,4,4096,4096,4096,4096};assert(!mosaic_client_set_batch_policy(&c,policy));
 c.frame_active=1;assert(mosaic_client_set_batch_policy(&c,policy)==-EBUSY);c.frame_active=0;
 policy[5]=0;assert(mosaic_client_set_batch_policy(&c,policy)<0);policy[5]=4097;assert(mosaic_client_set_batch_policy(&c,policy)<0);
 puts("MOSAIC_BATCH_COVERAGE_POLICY_HOST_PASS_GPU_RAW_CYCLES_NULL");return 0;
}
