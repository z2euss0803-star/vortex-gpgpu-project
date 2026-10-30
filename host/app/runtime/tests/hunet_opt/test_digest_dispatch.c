#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <vxl/digest.h>
static int capable,fail_alloc;
extern unsigned vxl_test_sha2_blocks;
void *__real_calloc(size_t,size_t);
void *__wrap_calloc(size_t n,size_t s){return fail_alloc?NULL:__real_calloc(n,s);}
int vxl_user_arch_sha2_capable(void){return capable;}
int main(void){
 const unsigned lengths[]={0,1,55,56,63,64,65,4095,4096,4097};
 unsigned char input[4100],ref[32],out[32];struct vxl_digest_ctx c={0};
 for(unsigned i=0;i<sizeof(input);i++)input[i]=(unsigned char)(i*17+9);
 capable=0;
 assert(!vxl_digest_begin(&c,VXL_DIGEST_AUTO));assert(c.actual_backend==VXL_DIGEST_PORTABLE&&c.fallback_used);
 assert(!vxl_digest_update(&c,input+1,4097));assert(!vxl_digest_final(&c,out));assert(!vxl_test_sha2_blocks);
 assert(vxl_digest_begin(&c,VXL_DIGEST_USER_SHA2)==-EOPNOTSUPP);assert(!vxl_test_sha2_blocks);
 capable=1;
 for(unsigned j=0;j<sizeof(lengths)/sizeof(lengths[0]);j++){
  unsigned n=lengths[j];assert(!vxl_digest_hash(input+1,n,ref,VXL_DIGEST_PORTABLE));
  assert(!vxl_digest_begin(&c,VXL_DIGEST_AUTO));assert(c.actual_backend==VXL_DIGEST_USER_SHA2&&!c.fallback_used);
  assert(!strcmp(c.implementation,"sha256-user-armv8-sha2"));
  for(unsigned i=0;i<n;i+=13)assert(!vxl_digest_update(&c,input+1+i,n-i<13?n-i:13));
  assert(!vxl_digest_final(&c,out));assert(!memcmp(out,ref,32));
 }
 assert(vxl_test_sha2_blocks);fail_alloc=1;
 assert(vxl_digest_begin(&c,VXL_DIGEST_AUTO)==-ENOMEM);assert(!c.fallback_used);
 assert(!c.backend_state&&c.state==VXL_DIGEST_FAILED);fail_alloc=0;
 puts("HOST_EMULATED_SHA2_DISPATCH_ALIGNMENT_AND_BOUNDARIES_PASS; NOT_A53_EXECUTED");return 0;
}
