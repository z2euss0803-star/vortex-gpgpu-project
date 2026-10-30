#ifndef VXL_DIGEST_H
#define VXL_DIGEST_H
#include <vxl/core.h>
enum vxl_digest_backend {VXL_DIGEST_AUTO,VXL_DIGEST_PORTABLE,VXL_DIGEST_USER_SHA2,VXL_DIGEST_KERNEL_CRYPTO};
enum vxl_digest_state {VXL_DIGEST_EMPTY,VXL_DIGEST_ACTIVE,VXL_DIGEST_DONE,VXL_DIGEST_FAILED};
struct vxl_digest_audit_row {vxl_u64 calls,bytes,failures,fallbacks;char implementation[64];};
struct vxl_digest_audit {struct vxl_digest_audit_row backend[4];vxl_u32 overflow;};
struct vxl_digest_ctx;
struct vxl_digest_ops {
 const char *name;enum vxl_digest_backend backend;
 int (*begin)(struct vxl_digest_ctx *);
 int (*update)(struct vxl_digest_ctx *,const void *,size_t);
 int (*final)(struct vxl_digest_ctx *,vxl_u8 [32]);
 void (*abort)(struct vxl_digest_ctx *);
};
/* Callers zero-initialize a fresh context; one context per transaction. */
struct vxl_digest_ctx {
 const struct vxl_digest_ops *ops;void *backend_state;
 enum vxl_digest_backend actual_backend;enum vxl_digest_state state;
 vxl_u64 bytes;int failure;vxl_u32 fallback_used,algorithm_unavailable;
 char implementation[64];
};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_digest_begin(struct vxl_digest_ctx *,enum vxl_digest_backend);
int vxl_digest_update(struct vxl_digest_ctx *,const void *,size_t);
int vxl_digest_final(struct vxl_digest_ctx *,vxl_u8 [32]);
void vxl_digest_abort(struct vxl_digest_ctx *);
int vxl_digest_hash(const void *,size_t,vxl_u8 [32],enum vxl_digest_backend);
void vxl_digest_audit_add(struct vxl_digest_audit *,const struct vxl_digest_ctx *);
const struct vxl_digest_ops *vxl_digest_portable_ops(void);
const struct vxl_digest_ops *vxl_digest_select(enum vxl_digest_backend);
int vxl_user_sha2_available(void);
const struct vxl_digest_ops *vxl_user_sha2_ops(void);
const struct vxl_digest_ops *vxl_kernel_crypto_ops(void);
#ifdef __cplusplus
}
#endif
#endif
