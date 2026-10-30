#include <vxl/v2/objects.h>
/* No CPU virtual address enters the address encoder: manifest pool + offset only. */
static int find_range(struct vxl_v2_ctx *c,vxl_u32 domain,vxl_u64 n,vxl_u64 *result){
 const vxl_u64 reserved[4][2]={{0x2000,0x1000},{0x30000,64},{VXL_V2_BOUNCE_OFFSET,4096},{0xff000,0x1000}};
 vxl_u64 off=0,next,cap=domain==VXL_PS?c->profile.ps_size:c->profile.pl_size;unsigned int i;
 for(;;){
  if(vxl_range_check(cap,off,n))return -ENOMEM;
  next=off;
  for(i=0;i<64;i++){
   const struct vxl_v2_object *o=&c->objects[i];
   if(o->handle && o->domain==domain && off<o->offset+o->allocated && o->offset<off+n && next<o->offset+o->allocated)next=o->offset+o->allocated;
  }
  if(domain==VXL_PS)for(i=0;i<4;i++)if(off<reserved[i][0]+reserved[i][1] && reserved[i][0]<off+n && next<reserved[i][0]+reserved[i][1])next=reserved[i][0]+reserved[i][1];
  if(next==off){*result=off;return 0;}off=next;
 }
}
int vxl_v2_buffer_create(struct vxl_v2_ctx *c,vxl_u64 owner,vxl_u64 epoch,vxl_u32 domain,vxl_u32 purpose,vxl_u32 access,vxl_u64 length,struct vxl_v2_ref *out){
 unsigned int i;vxl_u64 off,n,base;struct vxl_v2_object o;int rc=vxl_v2_owner_check(c,owner,epoch);if(rc)return rc;
 if(!out || (domain!=1 && domain!=2) || purpose<1 || purpose>4 || !access || (access & ~3u) || !length)return -EINVAL;
 if(c->guard_bytes && c->guard_bytes!=64)return -EINVAL;
 if(length>~(vxl_u64)0-63-2*c->guard_bytes)return -EOVERFLOW;
 if(c->serial==~(vxl_u64)0)return -EOVERFLOW;
 for(i=0;i<64;i++)if(!c->objects[i].handle)break;
 if(i==64)return -ENOSPC;
 n=((length+63)&~63ULL)+2*c->guard_bytes;rc=find_range(c,domain,n,&off);if(rc)return rc;
 base=domain==1?c->profile.ps_base:c->profile.pl_base;
 if(base>~(vxl_u64)0-off)return -EOVERFLOW;
 memset(&o,0,sizeof(o));o.guard_bytes=c->guard_bytes;o.bus=base+off+o.guard_bytes;rc=vxl_addr_encode(&c->profile,domain,o.bus,n-2*o.guard_bytes,&o.gpu);if(rc)return rc;
 o.handle=++c->serial;o.generation=o.handle;o.owner=owner;o.epoch=epoch;o.offset=off;o.length=length;o.allocated=n;o.domain=domain;o.purpose=purpose;o.access=access;
 if(c->ops && c->ops->allocate){rc=c->ops->allocate(c->memory_context,&o);if(rc)return rc;}
 c->objects[i]=o;c->live++;
 *out=(struct vxl_v2_ref){o.handle,o.generation,0,length,access,domain,epoch};return 0;
}
int vxl_v2_buffer_release(struct vxl_v2_ctx *c,vxl_u64 owner,const struct vxl_v2_ref *r){
 struct vxl_v2_object *o;unsigned int k;int rc=vxl_v2_ref_resolve(c,owner,r,0,&o);if(rc)return rc;
 if(r->offset || r->length!=o->length)return -EINVAL;
 for(k=0;k<5;k++)if(o->pins[k])return -EBUSY;
 if(c->ops && c->ops->free){rc=c->ops->free(c->memory_context,o);if(rc)return rc;}
 memset(o,0,sizeof(*o));c->live--;return 0;
}
int vxl_v2_buffer_batch(struct vxl_v2_ctx *c,vxl_u64 owner,vxl_u64 epoch,vxl_u32 domain,vxl_u64 length,vxl_u32 count,struct vxl_v2_ref *out){
 struct vxl_v2_ref tmp;unsigned int slots[64],i,k;int rc;
 if(!out || !count || count>64)return -EINVAL;
 for(i=0;i<count;i++){
  rc=vxl_v2_buffer_create(c,owner,epoch,domain,VXL_V2_DATA,3,length,&tmp);
  if(rc){
   while(i){struct vxl_v2_object *o;i--;o=&c->objects[slots[i]];tmp=(struct vxl_v2_ref){o->handle,o->generation,0,o->length,o->access,o->domain,o->epoch};
    if(vxl_v2_buffer_release(c,owner,&tmp)){c->quarantined=1;return -EIO;}}
   return rc;
  }
  for(k=0;k<64;k++)if(c->objects[k].handle==tmp.handle)break;
  slots[i]=k;
 }
 for(i=0;i<count;i++){const struct vxl_v2_object *o=&c->objects[slots[i]];out[i]=(struct vxl_v2_ref){o->handle,o->generation,0,o->length,o->access,o->domain,o->epoch};}
 return 0;
}
int vxl_v2_buffer_copy(struct vxl_v2_ctx *c,vxl_u64 owner,const struct vxl_v2_ref *r,void *bytes,int write){
 struct vxl_v2_object *o;unsigned int i;int rc;
 if(!bytes || (write!=0 && write!=1))return -EINVAL;
 rc=vxl_v2_ref_resolve(c,owner,r,write?2:1,&o);if(rc)return rc;
 /* Synchronous copies must not race an active/queued consumer. */
 for(i=0;i<5;i++)if(o->pins[i])return -EBUSY;
 if(!c->ops || !c->ops->copy)return -EOPNOTSUPP;
 rc=c->ops->copy(c->memory_context,o,r->offset,bytes,r->length,write);
 if(rc==-EIO || rc==-ETIMEDOUT)c->quarantined=1;
 return rc;
}
int vxl_v2_buffer_pin(struct vxl_v2_ctx *c,vxl_u64 owner,const struct vxl_v2_ref *r,vxl_u32 consumer){
 struct vxl_v2_object *o;int rc;
 if(consumer>=5)return -EINVAL;
 rc=vxl_v2_ref_resolve(c,owner,r,r?r->access:0,&o);if(rc)return rc;
 if(o->pins[consumer]==~(vxl_u32)0)return -EOVERFLOW;
 o->pins[consumer]++;return 0;
}
int vxl_v2_buffer_unpin(struct vxl_v2_ctx *c,vxl_u64 owner,const struct vxl_v2_ref *r,vxl_u32 consumer){
 struct vxl_v2_object *o;int rc;
 if(consumer>=5)return -EINVAL;
 rc=vxl_v2_ref_resolve(c,owner,r,0,&o);if(rc)return rc;
 if(!o->pins[consumer])return -EINVAL;
 o->pins[consumer]--;return 0;
}
int vxl_v2_buffer_seal(struct vxl_v2_ctx *c,vxl_u64 owner,const struct vxl_v2_ref *r){
 struct vxl_v2_object *o;unsigned int i;int rc=vxl_v2_ref_resolve(c,owner,r,0,&o);if(rc)return rc;
 if(r->offset || r->length!=o->length)return -EINVAL;
 for(i=0;i<5;i++)if(o->pins[i])return -EBUSY;
 o->sealed=1;return 0;
}
