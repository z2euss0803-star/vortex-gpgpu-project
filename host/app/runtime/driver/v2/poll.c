#include <vxl/v2/dispatch.h>
vxl_u32 vxl_v2_events_locked(struct vxl_v2_port *p,vxl_u64 owner){
 if(!p || !p->ctx || !owner || owner!=p->ctx->owner)return 8;
 return vxl_v2_poll_events(p->ctx);
}
int vxl_v2_close_locked(struct vxl_v2_port *p,vxl_u64 owner){
 unsigned int i;int rc;
 if(!p || !p->ctx)return -ENODEV;
 rc=vxl_v2_close_queue(p->ctx,owner);
 if(!rc && p->ctx->programs)for(i=0;i<16;i++){
  struct vxl_v2_program *program=&p->ctx->programs->programs[i];
  if(!program->ref.handle)continue;
  /* All load references belong to this closing fd, not to separate owners. */
  program->users=1;rc=vxl_v2_program_release(p->ctx,owner,&program->ref);if(rc)break;
 }
 if(!rc)rc=vxl_v2_owner_close(p->ctx,owner,1);
 if(rc){p->ctx->quarantined=1;p->ctx->closed=1;}
 p->revision++;p->ops->wake(p->os);return rc;
}
