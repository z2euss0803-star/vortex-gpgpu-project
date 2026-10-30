#include <vxl/v2/provider.h>
static int gate(struct vxl_v2_hw *h){
 if(!h || !h->ctx || !h->locked || !h->locked(h->os))return -EDEADLK;
 if(h->ctx->quarantined)return -EIO;
 if(!h->ps || h->ps_dma!=VXL_PS_BASE || h->ctx->profile.ps_base!=VXL_PS_BASE || h->ctx->profile.ps_size!=VXL_PS_SIZE || h->ctx->profile.pl_base!=VXL_PL_BASE || h->ctx->profile.pl_size!=VXL_PL_SIZE || !h->memory_barrier)return -ENODEV;
 return 0;
}
int vxl_v2_hw_idle(struct vxl_v2_hw *h){
 vxl_u32 status,sr;int rc=gate(h);if(rc)return rc;
 if(!h->read || !h->write || !h->ready || !h->cdma.read)return -ENODEV;
 if(h->read(h->os,0)!=0x56580204 || h->read(h->os,4)!=0x40404)return -ENODEV;
 status=h->read(h->os,0xc);sr=h->cdma.read(h->cdma.context,4);
 if((status&4) || h->read(h->os,0x1c) || (sr&0x4770))return -EIO;
 if(h->ready(h->os)!=1 || !(h->read(h->os,0x34)&1) || status || h->read(h->os,0x20) || h->read(h->os,0x24) || (h->read(h->os,0x58)&1) || !(sr&2))return -EBUSY;
 /* Cold IDLE is legal. Once our transfer completes, IOC is mandatory. */
 if(h->dma_completed && !(sr&0x1000))return -EIO;
 return 0;
}
int vxl_v2_prepare_tex(struct vxl_v2_hw *h,const struct vxl_v2_job *j,const struct vxl_v2_tex *t){
 struct vxl_v2_object *o;vxl_u64 footprint;vxl_u32 values[6]={0},offsets[6]={0x40,0x44,0x48,0x4c,0x54,0x50},i;int rc;
 if(!j || !t || t->enabled>1 || t->bilinear>1)return -EINVAL;
 rc=vxl_v2_hw_idle(h);if(rc)return rc;
 if(t->enabled){
  if(j->num_refs>8 || t->ref_index>=j->num_refs || !t->width || t->width>2048 || !t->height || t->height>2048 || t->pitch<t->width*4 || t->pitch>8192 || (t->pitch&3))return -EINVAL;
  rc=vxl_v2_ref_resolve(h->ctx,h->ctx->owner,&j->refs[t->ref_index],1,&o);if(rc)return rc;
  if(!o->pins[2] || (o->purpose!=VXL_V2_DATA && o->purpose!=VXL_V2_READONLY))return -EPERM;
  footprint=(vxl_u64)t->pitch*(t->height-1)+t->width*4;
  if(footprint>j->refs[t->ref_index].length || (vxl_u64)o->gpu+j->refs[t->ref_index].offset>0xffffffffULL)return -ERANGE;
  values[0]=o->gpu+(vxl_u32)j->refs[t->ref_index].offset;values[1]=t->width;values[2]=t->height;values[3]=t->pitch;values[4]=16;values[5]=8|t->bilinear;
 }else if(t->bilinear || t->ref_index || t->width || t->height || t->pitch)return -EINVAL;
 h->write(h->os,0x50,0);
 for(i=0;i<5;i++)h->write(h->os,offsets[i],values[i]);
 h->memory_barrier(h->os,1);h->write(h->os,0x50,values[5]);
 for(i=0;i<6;i++)if(h->read(h->os,offsets[i])!=values[i]){h->ctx->quarantined=1;return -EIO;}
 memcpy(h->tex_state,values,sizeof(values));return 0;
}
static int object_check(struct vxl_v2_hw *h,struct vxl_v2_object *o){
 vxl_u64 base;vxl_u32 gpu;int rc=gate(h);if(rc)return rc;
 if(!o || !o->allocated || (o->offset&63) || (o->allocated&63) || (o->domain!=1 && o->domain!=2) || o->purpose<1 || o->purpose>4)return -EINVAL;
 base=o->domain==VXL_PS?VXL_PS_BASE:VXL_PL_BASE;
 if((o->guard_bytes && o->guard_bytes!=64) || o->allocated<2*o->guard_bytes || o->length>o->allocated-2*o->guard_bytes)return -ERANGE;
 if(vxl_range_check(o->domain==VXL_PS?VXL_PS_SIZE:VXL_PL_SIZE,o->offset,o->allocated) || o->bus!=base+o->offset+o->guard_bytes)return -ERANGE;
 rc=vxl_addr_encode(&h->ctx->profile,o->domain,o->bus,o->allocated-2*o->guard_bytes,&gpu);if(rc || gpu!=o->gpu)return -ERANGE;
 if(o->domain==VXL_PS && o->offset<VXL_V2_BOUNCE_OFFSET+4096 && VXL_V2_BOUNCE_OFFSET<o->offset+o->allocated)return -ERANGE;
 return 0;
}
static int allocate(void *p,struct vxl_v2_object *o){
 struct vxl_v2_hw *h=p;int rc=object_check(h,o);if(rc)return rc;
 o->storage=o->domain==VXL_PS?(vxl_u8 *)h->ps+o->offset+o->guard_bytes:0;
 if(o->storage){memset((vxl_u8 *)h->ps+o->offset,0,(size_t)o->allocated);h->memory_barrier(h->os,1);}
 return o->guard_bytes?vxl_v2_native_guards(h,o,1):0;
}
static int release(void *p,struct vxl_v2_object *o){return object_check(p,o);}
static int block(struct vxl_v2_hw *h,vxl_u64 src,vxl_u64 dst,vxl_u32 n){
 vxl_u64 begin=h->now_ns?h->now_ns(h->os):0;int rc=vxl_cdma_run(&h->cdma,src,dst,n,1000);
 if(h->now_ns)h->cdma_ns+=h->now_ns(h->os)-begin;
 if(!rc){h->dma_completed=1;h->memory_barrier(h->os,0);}
 else if(rc==-EIO || rc==-ETIMEDOUT)h->ctx->quarantined=1;
 return rc;
}
static int copy(void *p,struct vxl_v2_object *o,vxl_u64 off,void *bytes,vxl_u64 n,int write){
 struct vxl_v2_hw *h=p;vxl_u8 *buffer=bytes,*bounce;vxl_u64 dma,nbytes,aligned,inside;int rc=object_check(h,o);if(rc)return rc;
 if(!bytes || !n || (write!=0 && write!=1) || vxl_range_check(o->allocated-2*o->guard_bytes,off,n))return -EINVAL;
 h->io_bytes+=n;
 if(o->domain==VXL_PS){
  if(o->storage!=(vxl_u8 *)h->ps+o->offset+o->guard_bytes)return -ERANGE;
  if(write){memcpy((vxl_u8 *)o->storage+off,bytes,(size_t)n);h->memory_barrier(h->os,1);}
  else {h->memory_barrier(h->os,0);memcpy(bytes,(vxl_u8 *)o->storage+off,(size_t)n);}return 0;
 }
 bounce=(vxl_u8 *)h->ps+VXL_V2_BOUNCE_OFFSET;dma=h->ps_dma+VXL_V2_BOUNCE_OFFSET;
 while(n){
  aligned=off&~15ULL;inside=off-aligned;
  if(inside || n<16){
   nbytes=16-inside;if(nbytes>n)nbytes=n;rc=block(h,o->bus+aligned,dma,16);if(rc)return rc;
   if(write){memcpy(bounce+inside,buffer,(size_t)nbytes);h->memory_barrier(h->os,1);rc=block(h,dma,o->bus+aligned,16);if(rc)return rc;}
   else memcpy(buffer,bounce+inside,(size_t)nbytes);
  }else{
   nbytes=n&~15ULL;if(nbytes>4096)nbytes=4096;
   if(write){memcpy(bounce,buffer,(size_t)nbytes);h->memory_barrier(h->os,1);rc=block(h,dma,o->bus+off,(vxl_u32)nbytes);}
   else {rc=block(h,o->bus+off,dma,(vxl_u32)nbytes);if(!rc)memcpy(buffer,bounce,(size_t)nbytes);}
   if(rc)return rc;
  }
  buffer+=nbytes;off+=nbytes;n-=nbytes;
 }
 return 0;
}
const struct vxl_v2_memory_ops vxl_v2_native_memory_ops={allocate,release,copy};
int vxl_v2_native_guards(struct vxl_v2_hw *h,struct vxl_v2_object *o,int write){
 struct vxl_v2_object raw;vxl_u8 bytes[64];int rc=object_check(h,o);if(rc)return rc;
 if(!o->guard_bytes || (write!=0 && write!=1))return -EINVAL;
 raw=*o;raw.guard_bytes=0;raw.bus-=64;raw.gpu-=64;raw.length=raw.allocated;
 if(raw.storage)raw.storage=(vxl_u8 *)raw.storage-64;
 for(vxl_u32 end=0;end<2;end++){
  vxl_u64 off=end?64+o->length:0,left=end?raw.allocated-off:64;
  while(left){vxl_u64 n=left>64?64:left;
   if(write)memset(bytes,0xa7,(size_t)n);
   rc=copy(h,&raw,off,bytes,n,write);if(rc)return rc;
   if(!write)for(vxl_u32 i=0;i<n;i++)if(bytes[i]!=0xa7)return -EBADMSG;
   off+=n;left-=n;
  }
 }
 return 0;
}
