#include <vxl/v2/jobs.h>
int vxl_v2_fail_queue(struct vxl_v2_ctx *c,int result,vxl_u64 now){
 struct vxl_v2_queue *q=c->queue;
 c->quarantined=1;q->fault_result=result;
 if(q->active_slot>=0){struct vxl_v2_slot *s=&q->slots[q->active_slot];q->fault_cookie=s->job.cookie;s->completion.cycles_valid=0;vxl_v2_finish_slot(q,s,VXL_V2_FAILED,result,now);}
 while(q->pending){struct vxl_v2_slot *s=&q->slots[q->fifo[q->head]];q->head=(q->head+1)%32;q->pending--;vxl_v2_finish_slot(q,s,VXL_V2_SKIPPED,-ECANCELED,now);}
 return result;
}
static int fail_start(struct vxl_v2_ctx *c,int result,vxl_u64 now){
 struct vxl_v2_queue *q=c->queue;struct vxl_v2_snapshot snapshot;
 if(q->snapshot && q->active_slot>=0){vxl_u64 sequence=q->slots[q->active_slot].completion.sequence;memset(&snapshot,0,sizeof(snapshot));q->snapshot(q->hardware,sequence,&snapshot);snapshot.sequence=sequence;if(!snapshot.now_ns)snapshot.now_ns=now;return vxl_v2_observe(c,&snapshot,result);}
 return vxl_v2_fail_queue(c,result,now);
}
int vxl_v2_pump(struct vxl_v2_ctx *c){
 struct vxl_v2_queue *q;struct vxl_v2_slot *s;vxl_u64 now;int rc;
 if(!c || !c->queue)return -EINVAL;
 q=c->queue;
 if(c->quarantined)return -EIO;
 if(c->active || q->active_slot>=0)return -EBUSY;
 if(!q->pending)return -EAGAIN;
 q->active_slot=(int)q->fifo[q->head];q->head=(q->head+1)%32;q->pending--;s=&q->slots[q->active_slot];c->active=1;
 now=q->ops->now_ns(q->hardware);
 if(s->job.execution_budget_ns>~(vxl_u64)0-now)return vxl_v2_fail_queue(c,-EOVERFLOW,now);
 rc=q->ops->prepare(q->hardware,s->program,&s->job,s->args,(vxl_u32)s->job.args_bytes);if(rc)return fail_start(c,rc,now);
 rc=q->ops->start(q->hardware,s->program,&s->job,&s->start);if(rc)return fail_start(c,rc,q->ops->now_ns(q->hardware));
 if(!s->start.started || !s->start.watchdog || s->start.start_ns<now || s->job.execution_budget_ns>~(vxl_u64)0-s->start.start_ns)return vxl_v2_fail_queue(c,-EIO,q->ops->now_ns(q->hardware));
 s->deadline_ns=s->start.start_ns+s->job.execution_budget_ns;s->state=VXL_V2_RUNNING;s->completion.state=VXL_V2_RUNNING;s->completion.start_ns=s->start.start_ns;return 0;
}
int vxl_v2_observe(struct vxl_v2_ctx *c,const struct vxl_v2_snapshot *s,int snapshot_result){
 struct vxl_v2_queue *q;struct vxl_v2_slot *slot;struct vxl_v2_completion *out;vxl_u32 idle=0,ready=0;int rc;
 if(!c || !c->queue || !s)return -EINVAL;
 q=c->queue;
 if(q->active_slot<0)return -EAGAIN;
 slot=&q->slots[q->active_slot];out=&slot->completion;
 if(s->sequence!=out->sequence)return -ESTALE;
 out->status=s->status;out->errors=s->errors;out->pending_w=s->pending_w;out->pending_r=s->pending_r;out->jobs=s->jobs;out->core_mask=s->core_mask;out->elapsed_raw=s->elapsed_raw;out->watchdog=s->watchdog;
 out->cdma_ns=s->cdma_ns;out->guard_ns=s->guard_ns;out->digest_ns=s->digest_ns;out->poll_ns=s->poll_ns;out->handoff_ns=s->handoff_ns;out->io_bytes=s->io_bytes;
 if(snapshot_result || c->quarantined)return vxl_v2_fail_queue(c,snapshot_result?snapshot_result:-EIO,s->now_ns);
 if(s->now_ns<out->start_ns)return vxl_v2_fail_queue(c,-EIO,s->now_ns);
 if(s->errors || (s->status&4))return vxl_v2_fail_queue(c,-EIO,s->now_ns);
 if(s->now_ns>=slot->deadline_ns)return vxl_v2_fail_queue(c,-ETIMEDOUT,s->now_ns);
 if(!s->done_seen || !(s->status&2))return 0;
 if(!out->done_seen_ns)out->done_seen_ns=s->now_ns;
 /* Preserve the old drain-before-ACK behavior; transient pending is not DONE. */
 if(s->pending_w || s->pending_r || s->jobs!=(vxl_u32)(slot->start.jobs_before+1))return 0;
 if(!s->cdma_idle || !s->guard_ok || s->exitcode || s->core_mask!=15 ||
    ((slot->job.flags&VXL_V2_JOB_FAST_RUNTIME)?!s->metadata_ok:(!s->parameters_ok || !s->source_ok)))return vxl_v2_fail_queue(c,-EIO,s->now_ns);
 if(!s->cycles_valid || !s->elapsed_raw || s->watchdog!=slot->start.watchdog || s->elapsed_raw>=s->watchdog)return vxl_v2_fail_queue(c,-ENODATA,s->now_ns);
 rc=q->ops->ack(q->hardware,&idle,&ready);out->ack_ns=q->ops->now_ns(q->hardware);
 if(rc || !idle || !ready)return fail_start(c,rc?rc:-EIO,out->ack_ns);
 if(q->metrics){const struct vxl_v2_snapshot *m=q->metrics;out->cdma_ns=m->cdma_ns;out->guard_ns=m->guard_ns;out->digest_ns=m->digest_ns;out->poll_ns=m->poll_ns;out->handoff_ns=m->handoff_ns;out->io_bytes=m->io_bytes;}
 out->cycles_valid=1;vxl_v2_finish_slot(q,slot,VXL_V2_OK,0,q->ops->now_ns(q->hardware));q->active_slot=-1;c->active=0;return 0;
}
int vxl_v2_poll(struct vxl_v2_ctx *c,const struct vxl_v2_snapshot *s){return vxl_v2_observe(c,s,0);}
int vxl_v2_close_queue(struct vxl_v2_ctx *c,vxl_u64 owner){
 if(!c || !c->queue || owner!=c->owner || !owner)return -EPERM;
 if(c->quarantined)return -EIO;
 if(c->active || c->queue->reserved)return vxl_v2_fail_queue(c,-ECANCELED,c->queue->ops->now_ns(c->queue->hardware));
 return 0;
}
