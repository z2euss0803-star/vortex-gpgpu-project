#include <vxl/v2/objects.h>
#include <vxl/engine.h>
static int locked(struct vxl_v2_adapter *a){return a && a->lock_held && a->lock_held(a->hardware);}
static struct vxl_object legacy_object(struct vxl_v2_object *o){
 struct vxl_object l={0};l.handle=o->handle;l.offset=o->offset;l.length=o->length;l.allocated=o->allocated;l.bus=o->bus;l.domain=o->domain;l.purpose=o->purpose;l.gpu=o->gpu;l.storage=o->storage;return l;
}
static int alloc_buffer(void *p,struct vxl_v2_object *o){
 struct vxl_v2_adapter *a=p;struct vxl_object l;int rc;
 if(!locked(a))return -EDEADLK;
 if(!a->transport || !a->transport->alloc)return -ENODEV;
 l=legacy_object(o);rc=a->transport->alloc(a->hardware,&l);if(!rc)o->storage=l.storage;return rc;
}
static int free_buffer(void *p,struct vxl_v2_object *o){
 struct vxl_v2_adapter *a=p;struct vxl_object l;
 if(!locked(a))return -EDEADLK;
 if(!a->transport || !a->transport->free)return -ENODEV;
 l=legacy_object(o);a->transport->free(a->hardware,&l);return 0;
}
static int copy_buffer(void *p,struct vxl_v2_object *o,vxl_u64 off,void *bytes,vxl_u64 n,int write){
 struct vxl_v2_adapter *a=p;struct vxl_object l;vxl_u8 tmp[16],*b=bytes;vxl_u64 amount,aligned;int rc;
 if(!locked(a))return -EDEADLK;
 if(!a->transport || !a->transport->transfer)return -ENODEV;
 l=legacy_object(o);
 while(n){
  if((off&15) || n<16){
   aligned=off&~15ULL;amount=16-(off&15);if(amount>n)amount=n;
   rc=a->transport->transfer(a->hardware,&l,aligned,tmp,16,0);if(rc)return rc;
   if(write){memcpy(tmp+(off&15),b,amount);rc=a->transport->transfer(a->hardware,&l,aligned,tmp,16,1);if(rc)return rc;}
   else memcpy(b,tmp+(off&15),amount);
  }else{
   amount=n&~15ULL;if(amount>4096)amount=4096;
   rc=a->transport->transfer(a->hardware,&l,off,b,amount,write);if(rc)return rc;
  }
  off+=amount;b+=amount;n-=amount;
 }
 return 0;
}
const struct vxl_v2_memory_ops vxl_v2_adapter_memory_ops={alloc_buffer,free_buffer,copy_buffer};
int vxl_v2_adapter_enter(struct vxl_v2_adapter *a,struct vxl_v2_ctx *c,vxl_u64 owner,vxl_u64 epoch){
 unsigned int i;
 if(!locked(a))return -EDEADLK;
 if(!a->legacy || !c || !a->transport || !a->transport->alloc || !a->transport->free || !a->transport->transfer)return -ENODEV;
 if(a->legacy->owner!=owner || !a->legacy->opened)return -EPERM;
 if(a->legacy->quarantined)return -EIO;
 if(a->legacy_debt || a->legacy->job.pinned || a->legacy->job.state!=VXL_READY)return -EBUSY;
 for(i=0;i<VXL_MAX_OBJECTS;i++)if(a->legacy->objects[i].handle)return -EBUSY;
 return vxl_v2_owner_enter(c,owner,epoch);
}
