#include <vxl/v2/jobs.h>
#include <vxl/hunet.h>
int vxl_v2_queue_bind(struct vxl_v2_ctx *c,struct vxl_v2_queue *q,const struct vxl_v2_execution_ops *ops,void *hardware){
 if(!c || !q || !ops || !ops->prepare || !ops->start || !ops->ack || !ops->now_ns)return -EINVAL;
 if(c->active || c->quarantined || c->queue)return -EBUSY;
 memset(q,0,sizeof(*q));q->active_slot=-1;q->ops=ops;q->hardware=hardware;q->owner_epoch=c->epoch;c->queue=q;return 0;
}
static int fill_refs(struct vxl_v2_ctx *c,struct vxl_v2_slot *s){
 unsigned int i;vxl_u32 domain=0;
 if(s->job.num_refs>8 || s->program->owned_count>5)return -EINVAL;
 for(i=0;i<64;i++)if(c->objects[i].handle==s->job.args_handle)domain=c->objects[i].domain;
 s->ref_count=0;
 for(i=0;i<s->job.num_refs;i++)s->refs[s->ref_count++]=s->job.refs[i];
 s->refs[s->ref_count++]=(struct vxl_v2_ref){s->job.args_handle,s->job.args_generation,s->job.args_offset,s->job.args_bytes,1,domain,s->job.device_generation};
 for(i=0;i<s->program->owned_count;i++)s->refs[s->ref_count++]=s->program->owned[i];
 return s->ref_count>14?-EINVAL:0;
}
int vxl_v2_batch_accept(struct vxl_v2_ctx *c,vxl_u64 owner,const struct vxl_v2_job *jobs,vxl_u32 n){
 struct vxl_v2_queue *q;unsigned int chosen[32],i,j,k,staged=0,pinned_jobs=0,pinned_refs=0;vxl_u64 previous,now;int rc;
 if(!c || !jobs || !n || n>32 || !c->queue || !c->programs)return -EINVAL;
 rc=vxl_v2_owner_check(c,owner,c->epoch);if(rc)return rc;q=c->queue;
 if(q->owner_epoch!=c->epoch){if(q->reserved || q->pending || q->active_slot>=0)return -EBUSY;q->owner_epoch=c->epoch;q->last_cookie=0;}
 if(n>32-q->pending || n>64-q->reserved)return -ENOSPC;
 if(n>~(vxl_u64)0-q->next_sequence)return -EOVERFLOW;
 previous=q->last_cookie;now=q->ops->now_ns(q->hardware);
 /* Validation and argument snapshots complete before any job is accepted. */
 for(i=0;i<n;i++){
  struct vxl_v2_slot *s;
  if(jobs[i].cookie<=q->last_cookie){struct vxl_v2_slot *old=0;rc=vxl_v2_slot_find(c,owner,0,jobs[i].cookie,&old)==0?-EALREADY:-ESTALE;goto rollback;}
  if(jobs[i].cookie<=previous){rc=-EINVAL;goto rollback;}previous=jobs[i].cookie;
  rc=vxl_v2_job_valid(&jobs[i]);if(rc)goto rollback;
  if((jobs[i].flags&VXL_V2_JOB_FAST_RUNTIME) && !q->fast_supported){rc=-EOPNOTSUPP;goto rollback;}
  if(jobs[i].execution_budget_ns>~(vxl_u64)0-now){rc=-EOVERFLOW;goto rollback;}
  for(j=0;j<64;j++)if(q->slots[j].state==VXL_V2_SLOT_FREE)break;
  if(j==64){rc=-ENOSPC;goto rollback;}
  chosen[staged++]=j;s=&q->slots[j];memset(s,0,sizeof(*s));s->state=VXL_V2_STAGED;s->job=jobs[i];
  for(k=0;k<16;k++){struct vxl_v2_program *p=&c->programs->programs[k];if(p->ref.handle==jobs[i].program_handle && p->ref.generation==jobs[i].program_generation && p->ref.device_generation==c->epoch){s->program=p;break;}}
  if(!s->program){rc=-ESTALE;goto rollback;}
  rc=vxl_v2_build_args(c,s->program,&s->job,s->args,sizeof(s->args));if(rc)goto rollback;
  rc=fill_refs(c,s);if(rc)goto rollback;
  s->completion.version=2;s->completion.bytes=256;s->completion.cookie=s->job.cookie;s->completion.device_generation=c->epoch;
  if(s->job.flags&VXL_V2_JOB_FAST_RUNTIME){
   const vxl_u8 policy[8]={VXL_V2_FAST_POLICY_VERSION,0,0,0,VXL_V2_FAST_OMITTED_CHECKS,0,0,0};
   s->completion.version=VXL_V2_COMPLETION_FAST_VERSION;s->completion.reserved0=VXL_V2_COMPLETION_FAST_UNCHECKED;
   memcpy(s->completion.reserved,policy,sizeof(policy));
  }
  s->completion.program_handle=s->program->ref.handle;s->completion.program_generation=s->program->ref.generation;s->completion.submit_ns=now;
  memcpy(s->completion.source_id,s->program->entry.raw_source,32);vxl_hunet_hash(s->args,s->job.args_bytes,s->completion.parameter_sha);
 }
 /* Take every implicit and explicit reference, rolling back the whole batch. */
 for(i=0;i<n;i++){
  struct vxl_v2_slot *s=&q->slots[chosen[i]];pinned_refs=0;
  if(s->program->pins==~(vxl_u32)0){rc=-EOVERFLOW;goto rollback;}
  for(j=0;j<s->ref_count;j++){rc=vxl_v2_buffer_pin(c,owner,&s->refs[j],2);if(rc)goto rollback;pinned_refs++;}
  s->program->pins++;pinned_jobs++;pinned_refs=0;
 }
 for(i=0;i<n;i++){struct vxl_v2_slot *s=&q->slots[chosen[i]];s->state=VXL_V2_QUEUED;s->completion.state=VXL_V2_QUEUED;s->completion.sequence=++q->next_sequence;q->fifo[q->tail]=chosen[i];q->tail=(q->tail+1)%32;q->pending++;q->reserved++;}
 q->last_cookie=previous;return 0;
rollback:
 if(pinned_refs && pinned_jobs<staged){struct vxl_v2_slot *s=&q->slots[chosen[pinned_jobs]];for(j=0;j<pinned_refs;j++)if(vxl_v2_buffer_unpin(c,owner,&s->refs[j],2))c->quarantined=1;}
 for(i=0;i<pinned_jobs;i++){struct vxl_v2_slot *s=&q->slots[chosen[i]];for(j=0;j<s->ref_count;j++)if(vxl_v2_buffer_unpin(c,owner,&s->refs[j],2))c->quarantined=1;s->program->pins--;}
 if(c->quarantined){q->fault_result=rc;q->fault_cookie=staged?q->slots[chosen[staged-1]].job.cookie:0;}
 for(i=0;i<staged;i++)memset(&q->slots[chosen[i]],0,sizeof(q->slots[0]));
 return rc;
}
int vxl_v2_accept(struct vxl_v2_ctx *c,vxl_u64 owner,const struct vxl_v2_job *j,vxl_u64 *sequence){
 int rc;if(!sequence)return -EINVAL;rc=vxl_v2_batch_accept(c,owner,j,1);if(!rc)*sequence=c->queue->next_sequence;return rc;
}
