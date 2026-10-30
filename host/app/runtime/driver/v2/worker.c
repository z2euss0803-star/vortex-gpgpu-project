#include <vxl/v2/dispatch.h>
int vxl_v2_worker_locked(struct vxl_v2_port *p){
 struct vxl_v2_ctx *c;struct vxl_v2_queue *q;struct vxl_v2_snapshot s;
 vxl_u32 completed,quarantined;int rc=0;
 if(!p || !p->ctx || !p->ctx->queue)return -ENODEV;
 c=p->ctx;q=c->queue;completed=q->completed;quarantined=c->quarantined;
 if(c->quarantined)return -EIO;
 if(q->active_slot>=0){
  memset(&s,0,sizeof(s));rc=p->ops->snapshot(p->os,q->slots[q->active_slot].completion.sequence,&s);
  /* A partial snapshot is still evidence when a later DMA/readback failed. */
  if(!s.sequence)s.sequence=q->slots[q->active_slot].completion.sequence;
  if(!s.now_ns)s.now_ns=p->ops->now_ns(p->os);
  rc=vxl_v2_observe(c,&s,rc);
 }
 if(!rc && !c->active && q->pending)rc=vxl_v2_pump(c);
 if(completed!=q->completed || quarantined!=c->quarantined){p->revision++;p->ops->wake(p->os);}
 return rc;
}
