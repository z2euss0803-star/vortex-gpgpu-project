#include "jobs_fixture.h"
int main(int argc,char **argv){struct fixture f;struct vxl_v2_job jobs[32];struct vxl_v2_completion out;unsigned i;
 assert(argc==2);setup(&f,argv[1]);for(i=0;i<32;i++)jobs[i]=job(&f,i+1);
 jobs[31].reserved[7]=1;assert(vxl_v2_batch_accept(&f.c,1,jobs,32)==-EINVAL);assert(!f.q->pending && !f.q->reserved && !f.hw.starts);jobs[31].reserved[7]=0;
 assert(vxl_v2_batch_accept(&f.c,1,jobs,32)==0 && f.q->pending==32 && f.q->reserved==32);assert(vxl_v2_batch_accept(&f.c,1,jobs,1)!=0);
 for(i=1;i<=32;i++){struct vxl_v2_snapshot s;assert(vxl_v2_pump(&f.c)==0);s=snapshot(&f,i);assert(vxl_v2_poll(&f.c,&s)==0);}
 for(i=0;i<32;i++)jobs[i]=job(&f,i+33);assert(vxl_v2_batch_accept(&f.c,1,jobs,32)==0 && f.q->reserved==64);
 for(i=33;i<=64;i++){struct vxl_v2_snapshot s;assert(vxl_v2_pump(&f.c)==0);s=snapshot(&f,i);assert(vxl_v2_poll(&f.c,&s)==0);}
 jobs[0]=job(&f,65);assert(vxl_v2_batch_accept(&f.c,1,jobs,1)==-ENOSPC);assert(f.hw.starts==64);
 assert(vxl_v2_read_completion(&f.c,1,64,64,&out)==0 && out.sequence==64);for(i=1;i<=64;i++)assert(vxl_v2_ack_reap(&f.c,1,i,i)==0);
 teardown_host_storage(&f);puts("V2_ATOMIC_BATCH32_COMPLETION_RESERVE64_PASS_NOT_BOARD");return 0;}
