#include "../vxl_internal.h"
#include "linux.h"
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
static void lock_port(void *p){mutex_lock(&((struct vxl_v2_linux *)p)->device->mutex);}
static void unlock_port(void *p){mutex_unlock(&((struct vxl_v2_linux *)p)->device->mutex);}
static int input_port(void *p,void *dst,vxl_u64 src,vxl_u32 bytes){(void)p;return copy_from_user(dst,u64_to_user_ptr(src),bytes)?-EFAULT:0;}
static int output_port(void *p,vxl_u64 dst,const void *src,vxl_u32 bytes){(void)p;return copy_to_user(u64_to_user_ptr(dst),src,bytes)?-EFAULT:0;}
static void *allocate_port(void *p,vxl_u32 bytes){(void)p;return kvmalloc(bytes,GFP_KERNEL);}
static void release_port(void *p,void *bytes){(void)p;kvfree(bytes);}
static vxl_u64 now_port(void *p){(void)p;return ktime_get_ns();}
static int sleep_port(void *p,vxl_u64 revision,vxl_u64 deadline){
 struct vxl_v2_linux *l=p;vxl_u64 now=ktime_get_ns();int rc;
 if(now>=deadline)return -ETIMEDOUT;
 rc=wait_event_interruptible_hrtimeout(l->waitq,READ_ONCE(l->port.revision)!=revision,ns_to_ktime(deadline-now));
 return rc==-ETIME?-ETIMEDOUT:rc;
}
static void wake_port(void *p){wake_up_interruptible(&((struct vxl_v2_linux *)p)->waitq);}
static void kick_port(void *p){struct vxl_v2_linux *l=p;mod_delayed_work(system_wq,&l->device->worker,0);}
static int snapshot_port(void *p,vxl_u64 seq,struct vxl_v2_snapshot *s){struct vxl_v2_linux *l=p;lockdep_assert_held(&l->device->mutex);return l->snapshot(l,seq,s);}
static const struct vxl_v2_port_ops linux_ops={lock_port,unlock_port,input_port,output_port,allocate_port,release_port,now_port,sleep_port,wake_port,kick_port,snapshot_port};
int vxl_v2_linux_bind(struct vxl_device *d,struct vxl_v2_linux *l,struct vxl_v2_ctx *c,int (*snapshot)(struct vxl_v2_linux *,vxl_u64,struct vxl_v2_snapshot *)){
 if(!d || !l || !c || !c->queue || !c->programs || !snapshot)return -EINVAL;
 if(d->compute || d->engine.owner || c->owner || c->active || c->live || c->quarantined)return -EBUSY;
 memset(l,0,sizeof(*l));l->device=d;l->snapshot=snapshot;l->port.ctx=c;l->port.ops=&linux_ops;l->port.os=l;init_waitqueue_head(&l->waitq);d->compute=l;return 0;
}
__poll_t vxl_v2_linux_poll(struct file *f,poll_table *wait){
 struct vxl_device *d=f->private_data;vxl_u32 events;__poll_t result=0;
 if(!d->compute)return 0; /* Legacy never claimed completion via poll. */
 poll_wait(f,&d->compute->waitq,wait);
 mutex_lock(&d->mutex);events=vxl_v2_events_locked(&d->compute->port,(unsigned long)f);mutex_unlock(&d->mutex);
 if(events&1)result|=EPOLLIN|EPOLLRDNORM;
 if(events&8)result|=EPOLLERR;
 return result;
}
