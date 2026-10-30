#include <vxl/digest.h>
#if defined(__aarch64__)
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif
/* Baseline ISA object. No crypto instructions may be emitted here. */
__attribute__((weak)) int vxl_user_arch_sha2_capable(void){
#if defined(__aarch64__)
 return !!(getauxval(AT_HWCAP)&HWCAP_SHA2);
#else
 return 0;
#endif
}
int vxl_user_sha2_available(void){return vxl_user_arch_sha2_capable();}
const struct vxl_digest_ops *vxl_digest_select(enum vxl_digest_backend b){
 if(b==VXL_DIGEST_PORTABLE)return vxl_digest_portable_ops();
 if(b==VXL_DIGEST_AUTO)return vxl_user_sha2_available()?vxl_user_sha2_ops():vxl_digest_portable_ops();
 if(b==VXL_DIGEST_USER_SHA2&&vxl_user_sha2_available())return vxl_user_sha2_ops();
 return NULL;
}
