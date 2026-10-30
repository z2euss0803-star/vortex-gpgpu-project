#include <vxl/mosaic.h>
#include "mosaic_contract_fixture.h"
#include <assert.h>
#include <stdio.h>
static void put(mosaic_u8 *p,mosaic_u32 x){p[0]=(mosaic_u8)x;p[1]=(mosaic_u8)(x>>8);p[2]=(mosaic_u8)(x>>16);p[3]=(mosaic_u8)(x>>24);}
static void seal(struct mosaic_job_v1 *j){put((mosaic_u8*)j+104,mosaic_job_crc(j));}
int main(void){
 struct mosaic_job_v1 job;struct mosaic_ctx ctx;struct mosaic_layout layout=fixture_layout,bad;
 unsigned i;const unsigned mutate[]={8,24,40,44,48,52,56,64,68,72,88};
 assert(mosaic_layout_check(&layout)==0);
 assert(mosaic_layout_check(&fixture_small_layout)==0);
 for(i=2;i<MOSAIC_ROLES;++i){bad=layout;bad.region[i].address=~(mosaic_u64)0-63;assert(mosaic_layout_check(&bad)<0);bad=layout;bad.region[i].bytes=~(mosaic_u64)0-63;assert(mosaic_layout_check(&bad)<0);bad=layout;bad.region[i].guard=0;assert(mosaic_layout_check(&bad)<0);}
 bad=layout;bad.width=1284;assert(mosaic_layout_check(&bad)<0);
 bad=layout;bad.region[MOSAIC_SKIP].bytes-=64;assert(mosaic_layout_check(&bad)<0);
 bad=layout;bad.region[MOSAIC_CODE].readonly=0;assert(mosaic_layout_check(&bad)<0);
 bad=layout;bad.region[MOSAIC_FP_B].address=bad.region[MOSAIC_FP_A].address;assert(mosaic_layout_check(&bad)<0);
 assert(mosaic_layout_check(NULL)<0);
 memcpy(&job,fixture_job,128);memset(&ctx,0,sizeof(ctx));ctx.expected=job;ctx.total_tiles=14400;ctx.layout=&layout;
 assert(mosaic_validate_job(&ctx,&job)==0);
 assert(memcmp(&ctx.expected,fixture_job,128)==0); /* no stage advance */
 for(i=0;i<sizeof(mutate)/sizeof(mutate[0]);++i){memcpy(&job,fixture_job,128);((mosaic_u8*)&job)[mutate[i]]^=1;seal(&job);assert(mosaic_validate_job(&ctx,&job)<0);}
 memcpy(&job,fixture_job,128);put((mosaic_u8*)&job+60,14401);seal(&job);assert(mosaic_validate_job(&ctx,&job)<0);
 put((mosaic_u8*)&job+60,0);seal(&job);assert(mosaic_validate_job(&ctx,&job)<0);
 memcpy(&job,fixture_job,128);put((mosaic_u8*)&job+108,2);seal(&job);assert(mosaic_validate_job(&ctx,&job)<0);
 memcpy(&job,fixture_job,128);put((mosaic_u8*)&job+108,1);seal(&job);assert(mosaic_validate_job(&ctx,&job)==0);
 job.reserved[15]=1;seal(&job);assert(mosaic_validate_job(&ctx,&job)<0);
 memcpy(&job,fixture_job,128);job.crc^=1;assert(mosaic_validate_job(&ctx,&job)<0);
 assert(mosaic_validate_job(NULL,&job)<0);assert(mosaic_validate_job(&ctx,NULL)<0);
 puts("MOSAIC_CONTRACT_LAYOUT_PASS_NOT_GPU_NOT_BOARD");return 0;
}
