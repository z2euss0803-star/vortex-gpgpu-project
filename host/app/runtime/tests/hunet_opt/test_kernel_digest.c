#include <assert.h>
#include <stdio.h>
#include <limits.h>
#include <crypto/hash.h>
#include <vxl/digest.h>
const struct vxl_digest_ops *vxl_kernel_crypto_ops(void);
static int mode,live,tfms,atomic_context,alloc_calls;
void *kzalloc(size_t n,unsigned f){(void)f;alloc_calls++;if(mode==1)return NULL;void *p=calloc(1,n);if(p)live++;return p;}
void kfree(void *p){if(p){live--;free(p);}}
struct crypto_shash *crypto_alloc_shash(const char *n,unsigned t,unsigned m){(void)t;(void)m;assert(!strcmp(n,"sha256"));if(mode==2)return (void *)(intptr_t)-ENOENT;if(mode==3)return (void *)(intptr_t)-ENOMEM;tfms++;return calloc(1,sizeof(struct crypto_shash));}
void crypto_free_shash(struct crypto_shash *t){tfms--;free(t);}
unsigned crypto_shash_descsize(struct crypto_shash *t){(void)t;return 0;}
int crypto_shash_init(struct shash_desc *d){if(mode==4)return -EIO;vxl_hunet_sha_init(&d->sha);return 0;}
int crypto_shash_update(struct shash_desc *d,const unsigned char *p,unsigned n){if(mode==5)return -EIO;vxl_hunet_sha_update(&d->sha,p,n);return 0;}
int crypto_shash_final(struct shash_desc *d,unsigned char *p){if(mode==6){memset(p,0,32);return -EIO;}vxl_hunet_sha_final(&d->sha,p);return 0;}
const char *crypto_tfm_alg_driver_name(struct crypto_shash *t){(void)t;return "test-sha256-generic";}
int in_atomic(void){return atomic_context;}
int in_interrupt(void){return 0;}
int irqs_disabled(void){return 0;}
const struct vxl_digest_ops *vxl_digest_select(enum vxl_digest_backend b){return b==VXL_DIGEST_PORTABLE?vxl_digest_portable_ops():vxl_kernel_crypto_ops();}
int main(void){
 unsigned char p[4097],out[32],ref[32];struct vxl_digest_ctx c={0};memset(p,0xa7,sizeof(p));vxl_hunet_hash(p,sizeof(p),ref);
 assert(!vxl_digest_begin(&c,VXL_DIGEST_AUTO));assert(c.actual_backend==VXL_DIGEST_KERNEL_CRYPTO);assert(!strcmp(c.implementation,"test-sha256-generic"));
 assert(!vxl_digest_update(&c,p+1,17));assert(!vxl_digest_update(&c,p+18,sizeof(p)-18));assert(!vxl_digest_final(&c,out));vxl_hunet_hash(p+1,sizeof(p)-1,ref);assert(!memcmp(out,ref,32));assert(!live&&!tfms);
 for(mode=1;mode<=6;mode++){
  memset(out,0x5a,32);int rc=vxl_digest_begin(&c,VXL_DIGEST_AUTO);
  if(mode==2){assert(!rc&&c.actual_backend==VXL_DIGEST_PORTABLE&&c.fallback_used);vxl_digest_abort(&c);}
  else if(mode<=4)assert(rc==(mode==1||mode==3?-ENOMEM:-EIO));
  else {assert(!rc);rc=vxl_digest_update(&c,p,sizeof(p));if(mode==5)assert(rc==-EIO);else {assert(!rc);assert(vxl_digest_final(&c,out)==-EIO);}}
  for(unsigned i=0;i<32;i++)assert(out[i]==0x5a);
  vxl_digest_abort(&c);vxl_digest_abort(&c);assert(!live&&!tfms);
 }
 mode=0;atomic_context=1;int before=alloc_calls;assert(vxl_digest_begin(&c,VXL_DIGEST_AUTO)==-EWOULDBLOCK);assert(before==alloc_calls);assert(!live&&!tfms);
 puts("HOST_SHASH_ADAPTER_FAULT_LIFETIME_CONTEXT_PASS; NOT_KERNEL_EXECUTED");return 0;
}
