#ifndef VXL_TEST_PORT_FIXTURE_H
#define VXL_TEST_PORT_FIXTURE_H
/* Catches reexecution after submit-return EFAULT, completion consumption on
 * read EFAULT, lost wakeups, and treating a wait timeout/signal as cancellation. */
#include "jobs_fixture.h"
#include <vxl/v2/dispatch.h>
#include <pthread.h>
struct os {struct fixture f;struct vxl_v2_port port;pthread_mutex_t mutex;int fault,interrupt,finish,locked,snapshot_fault;};
static inline void lock_os(void *p){struct os *o=p;assert(!pthread_mutex_lock(&o->mutex));o->locked=1;}
static inline void unlock_os(void *p){struct os *o=p;assert(o->locked);o->locked=0;assert(!pthread_mutex_unlock(&o->mutex));}
static inline int input_os(void *p,void *out,vxl_u64 in,vxl_u32 n){(void)p;if(!in)return -EFAULT;memcpy(out,(void *)(uintptr_t)in,n);return 0;}
static inline int output_os(void *p,vxl_u64 out,const void *in,vxl_u32 n){struct os *o=p;if(o->fault || !out)return -EFAULT;memcpy((void *)(uintptr_t)out,in,n);return 0;}
static inline void *allocate_os(void *p,vxl_u32 n){(void)p;return malloc(n);}
static inline void release_os(void *p,void *b){(void)p;free(b);}
static inline vxl_u64 now_os(void *p){return ((struct os *)p)->f.hw.now;}
static inline int snapshot_os(void *p,vxl_u64 seq,struct vxl_v2_snapshot *s){struct os *o=p;assert(o->locked);*s=snapshot(&o->f,seq);
#ifdef VXL_V2_FAST_RUNTIME_API
 if(o->f.q->active_slot>=0 && (o->f.q->slots[o->f.q->active_slot].job.flags&VXL_V2_JOB_FAST_RUNTIME)){s->metadata_ok=1;s->source_ok=s->parameters_ok=0;}
#endif
 if(!o->finish){s->status=1;s->done_seen=0;}return o->snapshot_fault?-EIO:0;}
static inline void wake_os(void *p){(void)p;}
static inline void kick_os(void *p){(void)p;}
static inline int sleep_os(void *p,vxl_u64 revision,vxl_u64 end){struct os *o=p;assert(!o->locked);if(o->interrupt)return -EINTR;
 /* Completion happens after WAIT unlocked but before the OS actually sleeps. */
 if(o->finish){int worker_rc,prior_slot;vxl_u64 prior_deadline;lock_os(o);prior_slot=o->f.q->active_slot;prior_deadline=prior_slot>=0?o->f.q->slots[prior_slot].deadline_ns:0;worker_rc=vxl_v2_worker_locked(&o->port);
  if(worker_rc)fprintf(stderr,"LATE_WAIT_WORKER_PROBE rc=%d now=%llu wait_end=%llu prior_slot=%d prior_deadline=%llu active=%u quarantine=%u completed=%u fault=%d slot_state=%u slot_result=%d revision=%llu prior_revision=%llu\n",worker_rc,(unsigned long long)o->f.hw.now,(unsigned long long)end,prior_slot,(unsigned long long)prior_deadline,o->f.c.active,o->f.c.quarantined,o->f.q->completed,o->f.q->fault_result,prior_slot>=0?o->f.q->slots[prior_slot].completion.state:0,prior_slot>=0?o->f.q->slots[prior_slot].completion.result:0,(unsigned long long)o->port.revision,(unsigned long long)revision);
  assert(worker_rc==0);assert(o->port.revision!=revision);unlock_os(o);return 0;}
 o->f.hw.now=end;return -ETIMEDOUT;}
static const struct vxl_v2_port_ops port_ops={lock_os,unlock_os,input_os,output_os,allocate_os,release_os,now_os,sleep_os,wake_os,kick_os,snapshot_os};
static inline struct vxl_compute_v2_request request(void){struct vxl_compute_v2_request r={.major=2,.size=128,.device_generation=1};return r;}
static inline int dispatch(struct os *o,unsigned int cmd,struct vxl_compute_v2_request *r){return vxl_v2_dispatch(&o->port,1,cmd,(vxl_u64)(uintptr_t)r);}

#endif
