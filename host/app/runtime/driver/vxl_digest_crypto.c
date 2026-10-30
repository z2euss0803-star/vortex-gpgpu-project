#include <crypto/hash.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <vxl/digest.h>
struct kernel_digest {struct crypto_shash *tfm;struct shash_desc *desc;};
static int allowed(void){return !in_atomic()&&!in_interrupt()&&!irqs_disabled();}
static void abort_hash(struct vxl_digest_ctx *c){
 struct kernel_digest *s=c->backend_state;if(!s)return;
 kfree(s->desc);if(s->tfm)crypto_free_shash(s->tfm);kfree(s);c->backend_state=NULL;
}
static int begin(struct vxl_digest_ctx *c){
 struct kernel_digest *s;const char *name;size_t n;int rc;
 if(!allowed())return -EWOULDBLOCK;
 s=kzalloc(sizeof(*s),GFP_KERNEL);if(!s)return -ENOMEM;c->backend_state=s;
 s->tfm=crypto_alloc_shash("sha256",0,0);
 if(IS_ERR(s->tfm)){rc=(int)PTR_ERR(s->tfm);s->tfm=NULL;c->algorithm_unavailable=rc==-ENOENT||rc==-ENODEV;return rc;}
 name=crypto_tfm_alg_driver_name(crypto_shash_tfm(s->tfm));n=strlen(name);
 if(n>=sizeof(c->implementation))return -ENAMETOOLONG;
 memcpy(c->implementation,name,n+1);
 s->desc=kzalloc(sizeof(*s->desc)+crypto_shash_descsize(s->tfm),GFP_KERNEL);if(!s->desc)return -ENOMEM;
 s->desc->tfm=s->tfm;return crypto_shash_init(s->desc);
}
static int update(struct vxl_digest_ctx *c,const void *p,size_t n){
 struct kernel_digest *s=c->backend_state;const unsigned char *v=p;int rc;
 if(!allowed())return -EWOULDBLOCK;
 while(n){unsigned part=n>(unsigned)-1?(unsigned)-1:(unsigned)n;rc=crypto_shash_update(s->desc,v,part);if(rc)return rc;v+=part;n-=part;}
 return 0;
}
static int final(struct vxl_digest_ctx *c,vxl_u8 out[32]){struct kernel_digest *s=c->backend_state;if(!allowed())return -EWOULDBLOCK;return crypto_shash_final(s->desc,out);}
static const struct vxl_digest_ops ops={"sha256-kernel-unselected",VXL_DIGEST_KERNEL_CRYPTO,begin,update,final,abort_hash};
const struct vxl_digest_ops *vxl_kernel_crypto_ops(void){return &ops;}
/* Strong selection overrides core's portable-only weak selector in kernel builds. */
#ifdef __KERNEL__
const struct vxl_digest_ops *vxl_digest_select(enum vxl_digest_backend b){
 if(b==VXL_DIGEST_PORTABLE)return vxl_digest_portable_ops();
 return b==VXL_DIGEST_AUTO||b==VXL_DIGEST_KERNEL_CRYPTO?&ops:NULL;
}
#endif
