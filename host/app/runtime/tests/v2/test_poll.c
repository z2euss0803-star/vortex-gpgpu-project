#include "jobs_fixture.h"
static int copy_fault(void *p,const void *bytes,vxl_u32 n){(void)p;(void)bytes;(void)n;return -EFAULT;}
int main(int argc,char **argv){struct fixture f;struct vxl_v2_job j;struct vxl_v2_snapshot s;struct vxl_v2_completion out;vxl_u64 seq;
 assert(argc==2);setup(&f,argv[1]);j=job(&f,1);assert(vxl_v2_accept(&f.c,1,&j,&seq)==0);assert(vxl_v2_pump(&f.c)==0);s=snapshot(&f,seq);assert(vxl_v2_poll(&f.c,&s)==0);
 assert(vxl_v2_poll_events(&f.c)==1 && vxl_v2_poll_events(&f.c)==1);assert(vxl_v2_deliver_completion(&f.c,1,seq,1,copy_fault,0)==-EFAULT);
 assert(vxl_v2_read_completion(&f.c,1,0,1,&out)==0 && out.sequence==seq);assert(vxl_v2_ack_reap(&f.c,1,seq,1)==0);assert(!vxl_v2_poll_events(&f.c));
 teardown_host_storage(&f);puts("V2_READ_COPY_FAULT_PRESERVES_COMPLETION_REPEATED_POLL_PASS_NOT_BOARD");return 0;}
