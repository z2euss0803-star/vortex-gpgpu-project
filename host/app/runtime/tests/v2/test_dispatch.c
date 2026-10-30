#include "port_fixture.h"
static void *race_worker(void *p){struct os *o=p;int rc;lock_os(o);rc=vxl_v2_worker_locked(&o->port);assert(rc==0 || rc==-EIO);unlock_os(o);return 0;}
static void *race_close(void *p){struct os *o=p;lock_os(o);assert(vxl_v2_close_locked(&o->port,1)==-ECANCELED);unlock_os(o);return 0;}
int main(int argc,char **argv){struct os o={0};struct vxl_compute_v2_request r;struct vxl_v2_completion out;struct vxl_v2_job j;vxl_u64 seq;
 assert(argc==2);setup(&o.f,argv[1]);assert(!pthread_mutex_init(&o.mutex,0));o.port.ctx=&o.f.c;o.port.ops=&port_ops;o.port.os=&o;j=job(&o.f,1);
 r=request();r.length=1;r.user_ptr=(vxl_u64)(uintptr_t)&j;r.payload_bytes=512;o.fault=1;
 assert(dispatch(&o,VXL_V2_SUBMIT_BATCH,&r)==-EFAULT);assert(o.f.q->reserved==1 && o.f.hw.starts==0);
 o.fault=0;assert(dispatch(&o,VXL_V2_SUBMIT_BATCH,&r)==-EALREADY);assert(o.f.q->reserved==1);seq=o.f.q->next_sequence;
 r=request();r.device_generation=2;r.cookie=1;r.user_ptr=(vxl_u64)(uintptr_t)&out;r.payload_bytes=256;
 assert(dispatch(&o,VXL_V2_WAIT,&r)==-ESTALE && o.f.q->reserved==1);
 assert(dispatch(&o,_IOW('V',0x89,struct vxl_compute_v2_request),&r)==-ENOTTY);
 lock_os(&o);assert(vxl_v2_worker_locked(&o.port)==0);unlock_os(&o);assert(o.f.hw.starts==1);
 r=request();r.sequence=seq;r.cookie=1;r.user_ptr=(vxl_u64)(uintptr_t)&out;r.payload_bytes=256;
 assert(dispatch(&o,VXL_V2_WAIT,&r)==-ETIMEDOUT);assert(o.f.c.active && o.f.q->reserved==1);
 r.length=1;o.interrupt=1;assert(dispatch(&o,VXL_V2_WAIT,&r)==-EINTR);assert(o.f.c.active && !o.f.c.quarantined);
 o.interrupt=0;o.finish=1;o.fault=1;assert(dispatch(&o,VXL_V2_WAIT,&r)==-EFAULT);assert(o.f.hw.starts==1 && o.f.q->completed==1);
 o.fault=0;r=request();r.flags=VXL_V2_READ;r.cookie=1;r.user_ptr=(vxl_u64)(uintptr_t)&out;r.payload_bytes=256;
 assert(dispatch(&o,VXL_V2_COMPLETE_REAP,&r)==0 && out.sequence==seq && out.elapsed_raw==37);
 lock_os(&o);assert(vxl_v2_events_locked(&o.port,1)==1 && vxl_v2_events_locked(&o.port,1)==1);unlock_os(&o);
 assert(dispatch(&o,VXL_V2_COMPLETE_REAP,&r)==0);r=request();r.flags=VXL_V2_ACK;r.sequence=seq;r.cookie=1;
 assert(dispatch(&o,VXL_V2_COMPLETE_REAP,&r)==0);assert(dispatch(&o,VXL_V2_COMPLETE_REAP,&r)==-ESTALE);
 lock_os(&o);assert(!vxl_v2_events_locked(&o.port,1));assert(vxl_v2_close_locked(&o.port,1)==0);unlock_os(&o);assert(!o.f.c.owner && !o.f.c.live);
 teardown_host_storage(&o.f);pthread_mutex_destroy(&o.mutex);
 /* Kernel-facing close and worker both use this exact single-mutex boundary.
  * Neither ordering may release completed-but-unacknowledged/active resources. */
 for(unsigned int i=0;i<100;i++){pthread_t a,b;memset(&o,0,sizeof(o));setup(&o.f,argv[1]);assert(!pthread_mutex_init(&o.mutex,0));o.port.ctx=&o.f.c;o.port.ops=&port_ops;o.port.os=&o;o.finish=1;
  j=job(&o.f,1);assert(vxl_v2_accept(&o.f.c,1,&j,&seq)==0);assert(vxl_v2_pump(&o.f.c)==0);
  assert(!pthread_create(&a,0,race_worker,&o));assert(!pthread_create(&b,0,race_close,&o));assert(!pthread_join(a,0));assert(!pthread_join(b,0));
  assert(o.f.c.quarantined && o.f.c.closed && o.f.c.live && o.f.q->reserved==1 && o.f.q->completed==1);assert(o.f.table.programs[0].pins==1);
  assert(o.f.hw.starts==1 && o.f.hw.acks<=1);teardown_host_storage(&o.f);pthread_mutex_destroy(&o.mutex);
 }
 /* A user WAIT deadline alone never quarantines. Only the worker's later
  * observation of the GPU budget expiration does so, with its evidence kept. */
 memset(&o,0,sizeof(o));setup(&o.f,argv[1]);assert(!pthread_mutex_init(&o.mutex,0));o.port.ctx=&o.f.c;o.port.ops=&port_ops;o.port.os=&o;
 j=job(&o.f,1);assert(vxl_v2_accept(&o.f.c,1,&j,&seq)==0);assert(vxl_v2_pump(&o.f.c)==0);
 r=request();r.length=1;r.sequence=seq;r.user_ptr=(vxl_u64)(uintptr_t)&out;r.payload_bytes=256;
 assert(dispatch(&o,VXL_V2_WAIT,&r)==-ETIMEDOUT && !o.f.c.quarantined && o.f.c.active);
 lock_os(&o);assert(vxl_v2_worker_locked(&o.port)==-ETIMEDOUT);assert(vxl_v2_events_locked(&o.port,1)==9);unlock_os(&o);
 r=request();assert(dispatch(&o,VXL_V2_FAULT_INFO,&r)==0 && r.result==-ETIMEDOUT && r.cookie==1);
 r=request();r.flags=VXL_V2_READ;r.sequence=seq;r.user_ptr=(vxl_u64)(uintptr_t)&out;r.payload_bytes=256;
 assert(dispatch(&o,VXL_V2_COMPLETE_REAP,&r)==0 && out.result==-ETIMEDOUT && !out.cycles_valid);
 assert(o.f.q->reserved==1 && o.f.hw.starts==1 && !o.f.hw.acks);teardown_host_storage(&o.f);pthread_mutex_destroy(&o.mutex);
 /* A failed post-DONE DMA/readback must retain the already sampled raw
  * registers, never replace the first failure with a zero completion. */
 memset(&o,0,sizeof(o));setup(&o.f,argv[1]);assert(!pthread_mutex_init(&o.mutex,0));o.port.ctx=&o.f.c;o.port.ops=&port_ops;o.port.os=&o;o.finish=1;o.snapshot_fault=1;
 j=job(&o.f,1);assert(vxl_v2_accept(&o.f.c,1,&j,&seq)==0);assert(vxl_v2_pump(&o.f.c)==0);
 lock_os(&o);assert(vxl_v2_worker_locked(&o.port)==-EIO);unlock_os(&o);
 assert(vxl_v2_read_completion(&o.f.c,1,seq,1,&out)==0 && out.status==2 && out.elapsed_raw==37 && !out.cycles_valid && out.result==-EIO);
 assert(o.f.q->reserved==1 && !o.f.hw.acks);teardown_host_storage(&o.f);pthread_mutex_destroy(&o.mutex);
 puts("V2_DISPATCH_EFAULT_WAIT_SIGNAL_WAKEUP_REAP_AND_100_CLOSE_RACES_PASS_NOT_BOARD");return 0;
}
