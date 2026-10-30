#include <vxl/profile_switch.h>
#include <vxl/hunet.h>
#include <vxl/s8.h>
#include <vxl/s8_trace.h>
#include <vxl/mosaic_host.h>
static int inactive(const struct vxl_engine *e){
 if(e->quarantined||e->job.pinned)return -EIO;
 if(e->job.state!=VXL_READY||e->job.id)return -EBUSY;
 for(unsigned i=0;i<VXL_MAX_OBJECTS;++i)if(e->objects[i].handle||e->objects[i].allocated||e->objects[i].storage)return -EBUSY;
 if(e->s8&&(e->s8->capture_pending||e->s8->delivery_fault||e->s8->error_class||vxl_s8_trace_open_allowed(e->s8)))return -EBUSY;
 if(e->hunet&&(e->hunet->active||e->hunet->end_pending||e->hunet->hybrid.owner||e->hunet->hybrid.end_pending||e->hunet->hybrid.cpu_input||e->hunet->hybrid.cpu_output||e->hunet->submit_time||e->hunet->mgmt_row))return -EBUSY;
 if(e->mosaic){const struct mosaic_host_state *h=e->mosaic->host;if(!h||h->first_error||h->loaded||h->frame_active||h->job_active||h->evidence_pending||h->pending_delivery.serial)return -EBUSY;}
 return 0;
}
int vxl_select_profile(struct vxl_engine *e,vxl_u64 owner,vxl_u32 target,vxl_u32 generation){
 if(!e||!e->profile_factory||!e->profile_factory->enabled)return -ENOTTY;
 struct vxl_profile_factory *f=e->profile_factory;
 if(!e->opened||!owner||owner!=e->owner)return -EBADF;
 if(generation!=e->generation)return -ESTALE;
 if(target!=4&&target!=5&&target!=6)return -EINVAL;
 if(e->quarantined||e->job.pinned)return -EIO;
 if(e->job.state!=VXL_READY||e->job.id)return -EBUSY;
 if(target==e->profile_id)return 0; /* same app keeps its resident objects */
 int rc=inactive(e);if(rc)return rc;
 if(e->generation==0xffffffffu)return -EOVERFLOW;
 if(!f->idle||!f->prepare||!f->discard||!f->commit)return -ENODEV;
 rc=f->idle(f->context,e);if(rc)return rc;
 struct vxl_engine *next=NULL;rc=f->prepare(f->context,target,e->generation+1,&next);
 if(rc){if(next)f->discard(f->context,next);return rc;}
 if(!next)return -ENODEV;
 if(next==e)return -EINVAL; /* trusted factory must never alias active storage */
 if(next->profile_id!=target||next->generation!=e->generation+1||next->owner||next->opened||next->ops!=e->ops||next->context!=e->context||memcmp(&next->profile,&e->profile,sizeof(e->profile))||inactive(next)||(target==4?(!next->hunet||next->s8||next->mosaic):target==5?(!next->s8||next->hunet||next->mosaic):(!next->mosaic||next->hunet||next->s8))){f->discard(f->context,next);return -ESTALE;}
 rc=f->idle(f->context,e);if(rc){f->discard(f->context,next);return rc;}
 next->owner=e->owner;next->opened=1;next->serial=e->serial;next->next_job=e->next_job;next->profile_factory=f;
 *e=*next;f->commit(f->context,next);return 0;
}
int vxl_select_request(struct vxl_engine *e,vxl_u64 owner,struct vxl_request *r){
 if(!e||!e->profile_factory||!e->profile_factory->enabled)return -ENOTTY;
 if(!r)return -EFAULT;
 if(r->abi_major!=1)return -EPROTONOSUPPORT;
 if(r->struct_size!=176||!r->generation)return -EINVAL;
 struct vxl_request rest=*r;rest.abi_major=rest.struct_size=rest.generation=rest.profile_id=0;const vxl_u8 *bytes=(const vxl_u8*)&rest;for(unsigned i=0;i<176;++i)if(bytes[i])return -EINVAL;
 int rc=vxl_select_profile(e,owner,r->profile_id,r->generation);if(!rc){r->profile_id=e->profile_id;r->generation=e->generation;r->state=VXL_READY;}return rc;
}

int vxl_demo_hunet_release(struct vxl_engine *e,vxl_u64 owner){
 if(!e||!e->profile_factory||!e->profile_factory->enabled)return -ENOTTY;
 if(!owner||owner!=e->owner||!e->opened)return -EBADF;
 if(e->profile_id!=4||!e->hunet)return -ENODEV;
 if(e->quarantined||e->job.pinned)return -EIO;
 if(e->job.state!=VXL_READY||e->job.id)return -EBUSY;
 struct vxl_hunet_ctx *c=e->hunet;struct vxl_hybrid_ctx *h=&c->hybrid;const struct vxl_hybrid_end_v1 *r=&h->end_receipt;
 if(!c->sealed)return c->active?-EBUSY:0;
 if(c->program_id!=9||!c->fused.release||!h->enabled||c->active||c->end_pending||h->end_pending||c->seal_owner!=owner||c->seal_generation!=e->generation||h->owner!=owner||h->generation!=e->generation||h->phase!=VXL_HYBRID_GPU_READY||h->logical_jobs!=1023||h->gpu_jobs!=103||h->cpu_jobs!=13||c->next_sequence!=1024||!c->evidence_delivered)return -EBUSY;
 if(r->magic!=VXL_HYBRID_END_MAGIC||r->version!=1||r->bytes!=256||r->flags||r->generation!=e->generation||r->logical_jobs!=1023||r->gpu_jobs!=103||r->cpu_jobs!=13||r->stages!=100||r->final_idle!=1||r->check_flags!=31||!r->last_gpu_job_id||r->last_gpu_job_id!=c->evidence.job_id||r->hw_jobs_after-r->hw_jobs_before!=103||r->hw_jobs_after!=e->snapshot.jobs||memcmp(r->run_id,c->run_id,16)||memcmp(r->input_sha256,c->input_sha,32)||memcmp(r->binding_sha256,c->fused.release->binding_sha,32))return -EBUSY;
 if(!e->profile_factory->idle)return -ENODEV;
 int rc=e->profile_factory->idle(e->profile_factory->context,e);if(rc)return rc;
 vxl_hybrid_cleanup(c);c->program_id=9;c->sealed=c->program_sealed=0;c->seal_owner=c->seal_generation=0;memset(c->handles,0,sizeof(c->handles));return 0;
}
