#include <vxl/demo_dispatch.h>
#include <errno.h>
static int idle_info(const struct vxl_request *r){return r->generation&&r->state==VXL_READY&&!r->errors&&!r->pending_r&&!r->pending_w&&!r->used;}
int demo_switch_idle(struct demo_session *s,enum demo_app app){
 if(!s||!s->dispatcher||(app!=DEMO_MOSAIC&&app!=DEMO_HUNET))return -EINVAL;
 struct demo_dispatch *d=s->dispatcher;if(s->quarantined||s->state==DEMO_RUNNING||s->state==DEMO_COMPUTE_DONE)return -EBUSY;
 int rc=demo_owner_acquire(s);if(rc)return rc;
 if(d->active){struct demo_gpu_ops *old=&d->apps[d->active-1];if(old->quiescent(old->context,s->owner_fd)!=1)return -EBUSY;if(d->active==app)return 0;rc=d->detach[d->active-1](old->context,s->owner_fd);if(rc)return rc;d->active=0;}
 struct vxl_request before={0};rc=d->control.info(d->control.context,s->owner_fd,&before);if(rc)return rc;
 if(!idle_info(&before))return -EBUSY;
 if(before.generation!=s->generation||before.profile_id!=d->profile)return -ESTALE;
 vxl_u32 target=app==DEMO_MOSAIC?6:4,next=0;rc=d->control.select(d->control.context,s->owner_fd,target,s->generation,&next);
 if(rc){struct vxl_request current={0};if(!d->control.info(d->control.context,s->owner_fd,&current)&&idle_info(&current)){s->generation=current.generation;d->profile=current.profile_id;}return rc;}
 struct vxl_request after={0};rc=d->control.info(d->control.context,s->owner_fd,&after);if(rc)return rc;
 if(!idle_info(&after)||after.profile_id!=target||after.generation!=next||(target!=before.profile_id?(before.generation==0xffffffffu||next!=before.generation+1):next!=before.generation))return -EPROTO;
 s->generation=next;d->profile=target;d->active=app;return 0;
}
int demo_select_app(struct demo_session *s,enum demo_app app){return demo_switch_idle(s,app);}
