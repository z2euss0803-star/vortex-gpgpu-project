#include "jobs_fixture.h"
int main(int argc,char **argv){struct fixture f;struct vxl_v2_job j;struct vxl_v2_snapshot s;struct vxl_v2_completion out={0};vxl_u64 seq=0;
 assert(argc==2);setup(&f,argv[1]);j=job(&f,1);assert(vxl_v2_accept(&f.c,1,&j,&seq)==0);assert(seq==1 && f.q->pending==1 && f.q->reserved==1 && !f.hw.starts);
 assert(vxl_v2_pump(&f.c)==0 && f.hw.starts==1 && f.c.active==1);assert(vxl_v2_pump(&f.c)==-EBUSY);
 s=snapshot(&f,seq);s.pending_w=1;assert(vxl_v2_poll(&f.c,&s)==0 && !f.hw.acks);s.pending_w=0;
 assert(vxl_v2_poll(&f.c,&s)==0 && f.hw.acks==1 && !f.c.active);
 assert(vxl_v2_read_completion(&f.c,1,seq,1,&out)==0 && out.state==VXL_V2_OK && out.cycles_valid && out.elapsed_raw==37);
 assert(out.start_ns<=out.done_seen_ns && out.done_seen_ns<=out.ack_ns && out.ack_ns<=out.complete_ns);
 assert(vxl_v2_poll(&f.c,&s)==-EAGAIN && f.hw.acks==1);assert(vxl_v2_accept(&f.c,1,&j,&seq)==-EALREADY);
 assert(vxl_v2_ack_reap(&f.c,1,1,1)==0 && !f.q->reserved);assert(vxl_v2_accept(&f.c,1,&j,&seq)==-ESTALE);
 {struct vxl_v2_job bad[2]={job(&f,2),job(&f,1)};unsigned i;assert(vxl_v2_batch_accept(&f.c,1,bad,2)==-ESTALE);for(i=0;i<64;i++)assert(f.q->slots[i].state==VXL_V2_SLOT_FREE);}
 teardown_host_storage(&f);puts("V2_SINGLE_ACTIVE_DONE_DRAIN_ACK_REPLAY_PASS_INJECTED_COUNTER_NOT_REAL_GPU");return 0;}
