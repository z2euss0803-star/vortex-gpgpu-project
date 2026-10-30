#include <vxl/v2/objects.h>
void vxl_v2_init(struct vxl_v2_ctx *c,const struct vxl_v2_memory_ops *ops,void *context){memset(c,0,sizeof(*c));c->ops=ops;c->memory_context=context;c->profile=(struct vxl_profile){VXL_PS_BASE,VXL_PS_SIZE,VXL_PL_BASE,VXL_PL_SIZE};}
int vxl_v2_owner_enter(struct vxl_v2_ctx *c,vxl_u64 owner,vxl_u64 epoch){
 if(!c || !owner)return -EINVAL;
 if(c->quarantined)return -EIO;
 if(c->owner || c->legacy_debt || c->active || c->live)return -EBUSY;
 if(c->epoch==~(vxl_u64)0)return -EOVERFLOW;
 if(!epoch || epoch<=c->epoch)return -ESTALE;
 c->owner=owner;c->epoch=epoch;c->closed=0;return 0;
}
int vxl_v2_owner_check(const struct vxl_v2_ctx *c,vxl_u64 owner,vxl_u64 epoch){
 if(!c)return -EINVAL;
 if(c->quarantined)return -EIO;
 if(!owner || c->owner!=owner)return -EPERM;
 if(c->epoch!=epoch)return -ESTALE;
 if(c->closed)return -EPIPE;
 return 0;
}
int vxl_v2_owner_close(struct vxl_v2_ctx *c,vxl_u64 owner,int stopped_known){
 unsigned int i,k;struct vxl_v2_ref r;int rc;
 if(!c || c->owner!=owner || !owner)return -EPERM;
 if(c->quarantined)return -EIO;
 if(!stopped_known || c->active){c->quarantined=1;c->closed=1;return -EIO;}
 for(i=0;i<64;i++)for(k=0;k<5;k++)if(c->objects[i].pins[k])return -EBUSY;
 for(i=0;i<64;i++)if(c->objects[i].handle){
  struct vxl_v2_object *o=&c->objects[i];
  r=(struct vxl_v2_ref){o->handle,o->generation,0,o->length,o->access,o->domain,o->epoch};
  rc=vxl_v2_buffer_release(c,owner,&r);if(rc)return rc;
 }
 c->owner=0;c->closed=1;return 0;
}
