#include "../hunet_hybrid/fixture.h"
#include <vxl/digest.h>
static int fault;
static int bad_begin(struct vxl_digest_ctx *c){(void)c;return -ENOMEM;}
static int bad_update(struct vxl_digest_ctx *c,const void *p,size_t n){(void)c;(void)p;(void)n;return -EIO;}
static int bad_final(struct vxl_digest_ctx *c,vxl_u8 out[32]){(void)c;(void)out;return -EIO;}
static void bad_abort(struct vxl_digest_ctx *c){(void)c;}
static const struct vxl_digest_ops bad={"fault-alloc",VXL_DIGEST_PORTABLE,bad_begin,bad_update,bad_final,bad_abort};
const struct vxl_digest_ops *vxl_digest_select(enum vxl_digest_backend b){(void)b;return fault?&bad:vxl_digest_portable_ops();}
int main(void){
 struct fixture old;init_fixture(&old);seal_fixture(&old);destroy_fixture(&old);
 struct hfixture h;float *x=malloc(1572864),*y=calloc(393216,4);assert(x&&y);
 hinit(&h);h.f.c.digest_enabled=1;struct vxl_hybrid_begin_v1 b=hbegin_request(&h,x);fault=1;
 assert(vxl_hybrid_begin_cpu(&h.f.c,&b)==-ENOMEM);assert(h.f.e.quarantined&&h.write_blocks==0&&h.f.c.hybrid.cpu_input);hfree(&h);
 fault=0;hinit(&h);h.f.c.digest_enabled=1;b=hbegin_request(&h,x);assert(!vxl_hybrid_begin_cpu(&h.f.c,&b));
 struct vxl_hybrid_commit_v1 c={.magic=VXL_HYBRID_COMMIT_MAGIC,.version=1,.bytes=128,.generation=b.generation,.sequence=b.sequence,.stage=b.stage,.algorithm=1,.token=b.token,.output_ptr=(uintptr_t)y,.output_bytes=b.output_bytes,.threads=4};memcpy(c.run_id,b.run_id,16);vxl_hunet_hash(y,1572864,c.output_sha256);
 fault=1;assert(vxl_hybrid_commit_cpu(&h.f.c,&c)==-ENOMEM);assert(h.f.e.quarantined&&h.write_blocks==0&&h.f.c.hybrid.cpu_input&&h.f.c.hybrid.cpu_output);hfree(&h);fault=0;
 free(x);free(y);puts("DIGEST_FAILURE_ABORTS_TRANSACTION_WITH_SNAPSHOTS_PINNED_PASS_NOT_BOARD");return 0;
}
