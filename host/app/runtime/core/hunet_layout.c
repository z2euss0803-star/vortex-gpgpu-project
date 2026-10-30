#include <vxl/engine.h>
#include <vxl/hunet.h>
#include "../generated/hunet/layout.h"
#include "../generated/hunet_fused/layout.h"
static int inside(vxl_u64 a,vxl_u64 n,vxl_u64 base,vxl_u64 size){return a>=base&&!vxl_range_check(size,a-base,n);}
int vxl_hunet_layout_init(struct vxl_hunet_ctx *c,struct vxl_engine *e,const struct vxl_hunet_release *r){
 int rc;if(!c||!e||!r||e->profile_id!=4)return -EINVAL;
 memset(c,0,sizeof(*c));c->engine=e;c->release=r;c->program_id=6;
 rc=vxl_pool_reserve(&e->pools[0],0x2000,4096);if(rc)return rc;
 rc=vxl_pool_reserve(&e->pools[0],0x10040,128);if(rc)return rc;
 rc=vxl_pool_reserve(&e->pools[0],0xff000,4096);if(rc)return rc;
 rc=vxl_pool_reserve(&e->pools[1],0x7ff00000,0x100000);if(rc)return rc;
 e->hunet=c;return 0;
}
int vxl_hunet_check_access(const struct vxl_hunet_ctx *c,vxl_u64 a,vxl_u64 n,vxl_u32 w){
 if(!c||!n)return -EINVAL;
 if(a>~(vxl_u64)0-n)return -EOVERFLOW;
 if((inside(a,n,VXL_HUNET_CODE,8388608)||inside(a,n,VXL_HUNET_WEIGHTS,67108864))&&(!w||!c->sealed))return 0;
 if(inside(a,n,VXL_HUNET_WS,VXL_HUNET_WS_BYTES)&&(!w||!c->sealed))return 0;
 if(!c->sealed&&inside(a,n,0x88000000,16384))return 0;
 return -EACCES;
}
int vxl_hunet_allocate_check(struct vxl_hunet_ctx *c,const struct vxl_request *r,int reserve){
 vxl_u64 base=0,size=0;
 if(c->sealed||c->active)return -EBUSY;
 if(r->purpose==VXL_ARGS)return r->domain==VXL_PS&&r->length==128&&(!reserve||!r->address)?0:-EINVAL;
 if(r->domain!=VXL_PL)return -EACCES;
 switch(r->purpose){case VXL_CODE:base=VXL_HUNET_CODE;size=8388608;break;case VXL_WEIGHTS:base=VXL_HUNET_WEIGHTS;size=67108864;break;case VXL_WORKSPACE:base=VXL_HUNET_WS;size=VXL_HUNET_WS_BYTES;break;case VXL_DATA:
  return reserve&&r->length<=16384&&r->address==0x88000000?0:(!reserve?0:-EACCES);
 default:return -EINVAL;}
 return reserve&&r->address==base&&r->length==size?0:-EINVAL;
}
/* The callback accepts aligned <=4096 byte blocks; preserve neighbours for odd byte copies. */
int vxl_hunet_io_bytes(struct vxl_hunet_ctx *c,vxl_u64 a,void *p,vxl_u64 n,int w){
 vxl_u8 tmp[16],*b=p;vxl_u64 take,aligned;int rc;
 if(!c||!c->io.rw||!n||a>~(vxl_u64)0-n)return -EINVAL;
 if(!inside(a,n,VXL_PS_BASE,VXL_PS_SIZE)&&!inside(a,n,0x80000000,0x80000000))return -ERANGE;
 while(n){if((a&15)||n<16){aligned=a&~15ULL;take=16-(a&15);if(take>n)take=n;rc=c->io.rw(c->io.context,aligned,tmp,16,0);if(rc)return rc;if(w){memcpy(tmp+(a&15),b,(unsigned)take);rc=c->io.rw(c->io.context,aligned,tmp,16,1);if(rc)return rc;}else memcpy(b,tmp+(a&15),(unsigned)take);}else{take=n&~15ULL;if(take>4096)take=4096;rc=c->io.rw(c->io.context,a,b,take,w);if(rc)return rc;}a+=take;b+=take;n-=take;}return 0;
}
int vxl_hunet_guards(struct vxl_hunet_ctx *c,int initialize){
 vxl_u8 buf[64];unsigned i,j,k;vxl_u64 a;int rc;
 for(i=0;i<106;i++)for(j=0;j<2;j++){
  a=j?vxl_hunet_slots[i].start+vxl_hunet_slots[i].bytes:vxl_hunet_slots[i].start-64;
  memset(buf,0xa5,sizeof(buf));rc=vxl_hunet_io_bytes(c,a,buf,sizeof(buf),initialize);if(rc)return rc;
  if(!initialize)for(k=0;k<64;k++)if(buf[k]!=0xa5)return -EIO;
 }
 if(c->program_id==9)for(i=0;i<3;i++){
  memset(buf,0xa5,sizeof(buf));
  rc=vxl_hunet_io_bytes(c,vxl_fused_guards[i],buf,sizeof(buf),initialize);if(rc)return rc;
  if(!initialize)for(k=0;k<64;k++)if(buf[k]!=0xa5)return -EIO;
 }
 if(c->program_id==10)return vxl_opt_guards(c,initialize);
 return 0;
}
