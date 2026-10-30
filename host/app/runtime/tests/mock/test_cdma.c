#include <vxl/core.h>
#include <assert.h>
#include <stdio.h>
struct model {vxl_u32 regs[16];unsigned btt,mode;vxl_u64 clock;};
static vxl_u32 rd(void *c,vxl_u32 off){struct model *m=c;if(off==4&&m->btt){if(m->mode==1)return 2;if(m->mode==2)return 0x4772;return 0x1002;}return m->regs[off/4];}
static void wr(void *c,vxl_u32 off,vxl_u32 v){struct model *m=c;if(off==4){assert(v==0x3000);if(m->mode!=3)m->regs[1]&=~v;}else{m->regs[off/4]=v;if(off==0x28)m->btt++;}}
static vxl_u64 now(void *c){return ((struct model *)c)->clock;}
static void pause(void *c){((struct model *)c)->clock++;}
int main(void){struct model m={0};struct vxl_cdma_io io={&m,rd,wr,now,pause,0};
 m.regs[1]=0x1002;assert(!vxl_cdma_run(&io,0x580020000ULL,0x10020000,16,10));
 assert(m.regs[0x1c/4]==5&&m.regs[0x18/4]==0x80020000&&m.btt==1);
 memset(&m,0,sizeof(m));m.regs[1]=0x1002;m.mode=1;assert(vxl_cdma_run(&io,16,32,16,10)==-ETIMEDOUT);assert(io.last_status==2);
 memset(&m,0,sizeof(m));m.regs[1]=0x1002;m.mode=2;assert(vxl_cdma_run(&io,16,32,16,10)==-EIO);assert(io.last_status==0x4772);
 memset(&m,0,sizeof(m));m.regs[1]=0x1002;m.mode=3;assert(vxl_cdma_run(&io,16,32,16,10)==-EIO&&!m.btt);
 memset(&m,0,sizeof(m));m.regs[1]=0;assert(vxl_cdma_run(&io,16,32,16,10)==-EBUSY&&!m.btt);
 memset(&m,0,sizeof(m));m.regs[1]=2;assert(vxl_cdma_run(&io,1ULL<<40,32,16,10)==-ERANGE&&!m.btt);
 assert(vxl_cdma_run(&io,16,32,17,10)==-EINVAL&&!m.btt);
 puts("SHARED_CDMA_OLD_IOC_IDLE_ERROR_TIMEOUT_HIGH_BITS_ORDER_PASS");return 0;}
