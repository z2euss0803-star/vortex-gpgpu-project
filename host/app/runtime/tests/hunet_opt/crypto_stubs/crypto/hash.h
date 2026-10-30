#ifndef TEST_CRYPTO_HASH_H
#define TEST_CRYPTO_HASH_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vxl/hunet.h>
#define GFP_KERNEL 0
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define PTR_ERR(p) ((long)(p))
struct crypto_shash {int reserved;};
struct shash_desc {struct crypto_shash *tfm;struct vxl_hunet_sha sha;};
struct crypto_shash *crypto_alloc_shash(const char *,unsigned,unsigned);
void crypto_free_shash(struct crypto_shash *);
unsigned crypto_shash_descsize(struct crypto_shash *);
int crypto_shash_init(struct shash_desc *);
int crypto_shash_update(struct shash_desc *,const unsigned char *,unsigned);
int crypto_shash_final(struct shash_desc *,unsigned char *);
#define crypto_shash_tfm(t) (t)
const char *crypto_tfm_alg_driver_name(struct crypto_shash *);
void *kzalloc(size_t,unsigned);
void kfree(void *);
int in_atomic(void);
int in_interrupt(void);
int irqs_disabled(void);
#endif
