#include "jobs_fixture.h"
int main(int argc,char **argv){struct fixture f;struct vxl_v2_job jobs[2];struct vxl_v2_completion a,b;struct vxl_v2_snapshot s;
 assert(argc==2);setup(&f,argv[1]);jobs[0]=job(&f,1);jobs[1]=job(&f,2);assert(vxl_v2_batch_accept(&f.c,1,jobs,2)==0);assert(vxl_v2_pump(&f.c)==0);
 s=snapshot(&f,1);s.guard_ok=0;assert(vxl_v2_poll(&f.c,&s)==-EIO && f.c.quarantined);assert(f.hw.starts==1 && f.hw.acks==0);
 assert(vxl_v2_read_completion(&f.c,1,1,1,&a)==0 && a.state==VXL_V2_FAILED);assert(vxl_v2_read_completion(&f.c,1,2,2,&b)==0 && b.state==VXL_V2_SKIPPED);
 assert(vxl_v2_ack_reap(&f.c,1,1,1)==-EIO);assert(vxl_v2_close_queue(&f.c,1)==-EIO && f.memory.live==3);
 teardown_host_storage(&f);puts("V2_FAULT_CLOSE_RETAINS_REFS_SKIPS_SUCCESSORS_PASS_NOT_BOARD");return 0;}
