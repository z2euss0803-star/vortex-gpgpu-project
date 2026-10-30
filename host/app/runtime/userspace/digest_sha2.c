#include <stdlib.h>
#include <vxl/digest.h>
#include <vxl/hunet.h>
extern void vxl_sha2_compress(vxl_u32 h[8],const vxl_u8 p[64]);
static void sha2_update(struct vxl_hunet_sha *s,const vxl_u8 *p,size_t n){
 unsigned off=(unsigned)(s->count&63);
 while(n){size_t take=64-off;if(take>n)take=n;memcpy(s->block+off,p,take);s->count+=take;p+=take;n-=take;off+=(unsigned)take;if(off==64){vxl_sha2_compress(s->h,s->block);off=0;}}
}
static int begin(struct vxl_digest_ctx *c){
 c->backend_state=calloc(1,sizeof(struct vxl_hunet_sha));if(!c->backend_state)return -ENOMEM;
 vxl_hunet_sha_init(c->backend_state);return 0;
}
static int update(struct vxl_digest_ctx *c,const void *p,size_t n){sha2_update(c->backend_state,p,n);return 0;}
static int final(struct vxl_digest_ctx *c,vxl_u8 out[32]){
 struct vxl_hunet_sha *s=c->backend_state;vxl_u64 bits=s->count*8;vxl_u8 tail[72]={0x80};
 unsigned off=(unsigned)(s->count&63),n=off<56?56-off:120-off;
 for(unsigned i=0;i<8;i++)tail[n+i]=(vxl_u8)(bits>>(56-8*i));
 sha2_update(s,tail,n+8);
 for(unsigned i=0;i<32;i++)out[i]=(vxl_u8)(s->h[i/4]>>(24-8*(i%4)));
 return 0;
}
static void abort_hash(struct vxl_digest_ctx *c){if(c->backend_state){memset(c->backend_state,0,sizeof(struct vxl_hunet_sha));free(c->backend_state);c->backend_state=NULL;}}
static const struct vxl_digest_ops ops={"sha256-user-armv8-sha2",VXL_DIGEST_USER_SHA2,begin,update,final,abort_hash};
const struct vxl_digest_ops *vxl_user_sha2_ops(void){return &ops;}
