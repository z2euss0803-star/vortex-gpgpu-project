/* ARM ACLE SHA256H/H2 semantics: https://arm-software.github.io/acle/neon_intrinsics/advsimd.html
 * Message bytes are explicitly big endian. H2 consumes the pre-H ABCD value.
 * Shared production algorithm; host emulation supplies only intrinsic semantics.
 */
#include <stdint.h>
static uint32_t sha2_rot(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
static void vxl_sha2_compress_impl(uint32_t h[8],const unsigned char p[64]){
 static const uint32_t k[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 uint32_t w[64];
 for(unsigned i=0;i<16;i++)w[i]=((uint32_t)p[4*i]<<24)|((uint32_t)p[4*i+1]<<16)|((uint32_t)p[4*i+2]<<8)|p[4*i+3];
 for(unsigned i=16;i<64;i++)w[i]=w[i-16]+(sha2_rot(w[i-15],7)^sha2_rot(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(sha2_rot(w[i-2],17)^sha2_rot(w[i-2],19)^(w[i-2]>>10));
 uint32x4_t a=vld1q_u32(h),e=vld1q_u32(h+4),a0=a,e0=e;
 for(unsigned i=0;i<64;i+=4){
  uint32x4_t wk=vaddq_u32(vld1q_u32(w+i),vld1q_u32(k+i)),old_a=a;
  a=vsha256hq_u32(a,e,wk);e=vsha256h2q_u32(e,old_a,wk);
 }
 vst1q_u32(h,vaddq_u32(a,a0));vst1q_u32(h+4,vaddq_u32(e,e0));
}
