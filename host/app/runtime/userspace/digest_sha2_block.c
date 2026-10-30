/* The only object built with +crypto. Dispatch must check HWCAP_SHA2 first. */
#include <arm_neon.h>
#include "digest_sha2_block.h"
void vxl_sha2_compress(uint32_t h[8],const unsigned char p[64]){vxl_sha2_compress_impl(h,p);}
