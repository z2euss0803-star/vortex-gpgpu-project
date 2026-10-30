#include "sha2_emulation.h"
#include "../../userspace/digest_sha2_block.h"
unsigned vxl_test_sha2_blocks;
void vxl_sha2_compress(uint32_t h[8],const unsigned char p[64]){vxl_test_sha2_blocks++;vxl_sha2_compress_impl(h,p);}
