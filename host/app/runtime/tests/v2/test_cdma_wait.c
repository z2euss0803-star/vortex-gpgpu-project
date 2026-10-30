/* Actual common CDMA transaction with the V2-only wait policy and MMIO fake. */
#include <assert.h>
#include <stdio.h>
#include "../../driver/v2/cdma_wait.h"
struct wait_fixture {
 struct vxl_v2_cdma_wait wait;vxl_u64 ns;unsigned active,polls,complete_after,error_after;
 unsigned fast,slow,frozen_clock;
};
static vxl_u32 status_read(void *p,vxl_u32 off){
 struct wait_fixture *f=p;assert(off==4);
 if(!f->active)return 2;
 f->polls++;if(f->error_after && f->polls>=f->error_after)return 0x10;
 return f->complete_after && f->polls>=f->complete_after?0x1002:2;
}
static void transaction_write(void *p,vxl_u32 off,vxl_u32 value){
 struct wait_fixture *f=p;(void)value;
 if(off==4)f->active=0;
 if(off==0x28){f->polls=0;f->active=1;vxl_v2_cdma_wait_begin(&f->wait,f->ns);}
}
static vxl_u64 milliseconds(void *p){return ((struct wait_fixture *)p)->ns/1000000;}
static void transaction_pause(void *p){
 struct wait_fixture *f=p;
 if(vxl_v2_cdma_wait_short(&f->wait,f->ns)){f->fast++;if(!f->frozen_clock)f->ns+=1000;}
 else {f->slow++;f->ns+=1000000;}
}
static int run(struct wait_fixture *f,vxl_u64 timeout){
 struct vxl_cdma_io io={f,status_read,transaction_write,milliseconds,transaction_pause,0};
 return vxl_cdma_run(&io,VXL_PS_BASE,VXL_PL_BASE,16,timeout);
}
int main(void){
 struct wait_fixture f={.complete_after=4};assert(run(&f,1000)==0);
 printf("short completion: fast=%u slow=%u expected=3/0\n",f.fast,f.slow);fflush(stdout);
 assert(f.fast==3 && !f.slow);
 f=(struct wait_fixture){.complete_after=80};assert(run(&f,1000)==0 && f.fast==50 && f.slow==29);
 f.fast=f.slow=0;f.complete_after=4;assert(run(&f,1000)==0 && f.fast==3 && !f.slow);
 f=(struct wait_fixture){.frozen_clock=1};assert(run(&f,5)==-ETIMEDOUT && f.fast==50 && f.slow==5);
 f=(struct wait_fixture){.error_after=3};assert(run(&f,1000)==-EIO && f.fast==2 && !f.slow);
 f=(struct wait_fixture){0};vxl_v2_cdma_wait_begin(&f.wait,10);
 assert(!vxl_v2_cdma_wait_short(&f.wait,9)); /* Clock regression cannot spin. */
 assert(!vxl_v2_cdma_wait_short(&f.wait,50010));
 puts("V2_CDMA_BOUNDED_SHORT_THEN_SLEEP_ERROR_IOC_TIMEOUT_PASS_HOST_NOT_BOARD");return 0;
}
