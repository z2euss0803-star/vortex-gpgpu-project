/* Native software tests have no ARM instructions or pretend SHA2 execution. */
#include <vxl/digest.h>
#ifdef __aarch64__
#error Target AArch64 must link the real isolated SHA2 implementation
#endif
const struct vxl_digest_ops *vxl_user_sha2_ops(void){return NULL;}
