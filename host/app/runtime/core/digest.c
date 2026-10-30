#include <vxl/digest.h>
#include <vxl/hunet.h>
#ifdef __KERNEL__
#include <linux/slab.h>
static void *digest_alloc(size_t n){return kzalloc(n,GFP_KERNEL);}
static void digest_free(void *p){kfree(p);}
#else
#include <stdlib.h>
static void *digest_alloc(size_t n){return calloc(1,n);}
static void digest_free(void *p){free(p);}
#endif
static int portable_begin(struct vxl_digest_ctx *c){c->backend_state=digest_alloc(sizeof(struct vxl_hunet_sha));if(!c->backend_state)return -ENOMEM;vxl_hunet_sha_init(c->backend_state);return 0;}
static int portable_update(struct vxl_digest_ctx *c,const void *p,size_t n){vxl_hunet_sha_update(c->backend_state,p,n);return 0;}
static int portable_final(struct vxl_digest_ctx *c,vxl_u8 out[32]){vxl_hunet_sha_final(c->backend_state,out);return 0;}
static void portable_abort(struct vxl_digest_ctx *c){digest_free(c->backend_state);c->backend_state=NULL;}
static const struct vxl_digest_ops portable={"sha256-portable",VXL_DIGEST_PORTABLE,portable_begin,portable_update,portable_final,portable_abort};
const struct vxl_digest_ops *vxl_digest_portable_ops(void){return &portable;}
__attribute__((weak)) const struct vxl_digest_ops *vxl_digest_select(enum vxl_digest_backend b){return b==VXL_DIGEST_AUTO||b==VXL_DIGEST_PORTABLE?&portable:NULL;}
static void release(struct vxl_digest_ctx *c){if(c->backend_state&&c->ops&&c->ops->abort)c->ops->abort(c);c->backend_state=NULL;}
static int fail(struct vxl_digest_ctx *c,int rc){release(c);c->state=VXL_DIGEST_FAILED;c->failure=rc;return rc;}
static int select_ops(struct vxl_digest_ctx *c,const struct vxl_digest_ops *ops){
 size_t n;if(!ops)return -EOPNOTSUPP;
 if(!ops->name||!ops->begin||!ops->update||!ops->final||!ops->abort)return -EINVAL;
 n=strlen(ops->name);if(n>=sizeof(c->implementation))return -EINVAL;
 c->ops=ops;c->actual_backend=ops->backend;memcpy(c->implementation,ops->name,n+1);return 0;
}
int vxl_digest_begin(struct vxl_digest_ctx *c,enum vxl_digest_backend backend){
 int rc;if(!c)return -EFAULT;if(c->state==VXL_DIGEST_ACTIVE)return -EBUSY;
 memset(c,0,sizeof(*c));rc=select_ops(c,vxl_digest_select(backend));if(rc)return fail(c,rc);
 c->fallback_used=backend==VXL_DIGEST_AUTO&&c->actual_backend==VXL_DIGEST_PORTABLE;
 rc=c->ops->begin(c);
 if(rc&&backend==VXL_DIGEST_AUTO&&c->algorithm_unavailable&&(rc==-ENOENT||rc==-ENODEV)){
  release(c);rc=select_ops(c,&portable);if(rc)return fail(c,rc);c->fallback_used=1;c->algorithm_unavailable=0;rc=c->ops->begin(c);
 }
 if(rc)return fail(c,rc);
 c->state=VXL_DIGEST_ACTIVE;return 0;
}
int vxl_digest_update(struct vxl_digest_ctx *c,const void *data,size_t bytes){
 int rc;if(!c||c->state!=VXL_DIGEST_ACTIVE)return -EINVAL;
 if(bytes&&!data)return fail(c,-EFAULT);
 if(c->bytes>~(vxl_u64)0/8||(vxl_u64)bytes>~(vxl_u64)0/8-c->bytes)return fail(c,-EOVERFLOW);
 if(!bytes)return 0;
 rc=c->ops->update(c,data,bytes);if(rc)return fail(c,rc);c->bytes+=bytes;return 0;
}
int vxl_digest_final(struct vxl_digest_ctx *c,vxl_u8 out[32]){
 vxl_u8 private_out[32];int rc;
 if(!c||c->state!=VXL_DIGEST_ACTIVE)return -EINVAL;
 if(!out)return fail(c,-EFAULT);
 rc=c->ops->final(c,private_out);if(rc)return fail(c,rc);
 release(c);c->state=VXL_DIGEST_DONE;memcpy(out,private_out,32);return 0;
}
void vxl_digest_abort(struct vxl_digest_ctx *c){if(!c)return;release(c);if(c->state==VXL_DIGEST_ACTIVE){c->state=VXL_DIGEST_FAILED;c->failure=-ECANCELED;}}
int vxl_digest_hash(const void *p,size_t n,vxl_u8 out[32],enum vxl_digest_backend backend){
 struct vxl_digest_ctx c={0};int rc=vxl_digest_begin(&c,backend);if(!rc)rc=vxl_digest_update(&c,p,n);if(!rc)rc=vxl_digest_final(&c,out);vxl_digest_abort(&c);return rc;
}
int vxl_hunet_digest_init(struct vxl_hunet_ctx *c,struct vxl_hunet_digest_ctx *s){
 memset(s,0,sizeof(*s));s->enabled=c&&c->digest_enabled;
 if(s->enabled){int rc;s->audit=&c->opt.digest_audit;rc=vxl_digest_begin(&s->modern,c->digest_backend);if(rc){vxl_digest_audit_add(s->audit,&s->modern);s->audit=NULL;}return rc;}
 vxl_hunet_sha_init(&s->legacy);return 0;
}
int vxl_hunet_digest_update(struct vxl_hunet_digest_ctx *s,const void *p,vxl_u64 n){
 if(n>(size_t)-1)return -EOVERFLOW;
 if(s->enabled)return vxl_digest_update(&s->modern,p,(size_t)n);
 vxl_hunet_sha_update(&s->legacy,p,n);return 0;
}
int vxl_hunet_digest_final(struct vxl_hunet_digest_ctx *s,vxl_u8 out[32]){
 if(s->enabled)return vxl_digest_final(&s->modern,out);
 vxl_hunet_sha_final(&s->legacy,out);return 0;
}
void vxl_digest_audit_add(struct vxl_digest_audit *a,const struct vxl_digest_ctx *c){
 struct vxl_digest_audit_row *r;
 if(!a||!c||c->actual_backend>3)return;
 r=&a->backend[c->actual_backend];
 if(r->calls==~(vxl_u64)0||r->bytes>~(vxl_u64)0-c->bytes||r->failures==~(vxl_u64)0||r->fallbacks==~(vxl_u64)0){a->overflow=1;return;}
 r->calls++;r->bytes+=c->bytes;r->failures+=c->state!=VXL_DIGEST_DONE;r->fallbacks+=!!c->fallback_used;
 if(r->implementation[0]&&memcmp(r->implementation,c->implementation,64))a->overflow=1;
 memcpy(r->implementation,c->implementation,64);
}
void vxl_hunet_digest_abort(struct vxl_hunet_digest_ctx *s){if(s->enabled){vxl_digest_abort(&s->modern);vxl_digest_audit_add(s->audit,&s->modern);s->audit=NULL;}}
int vxl_hunet_digest_hash(struct vxl_hunet_ctx *c,const void *p,vxl_u64 n,vxl_u8 out[32]){
 struct vxl_hunet_digest_ctx s;int rc=vxl_hunet_digest_init(c,&s);if(!rc)rc=vxl_hunet_digest_update(&s,p,n);if(!rc)rc=vxl_hunet_digest_final(&s,out);vxl_hunet_digest_abort(&s);return rc;
}
