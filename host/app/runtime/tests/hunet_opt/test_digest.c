#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <vxl/digest.h>
static int mode,live;
static int fault_begin(struct vxl_digest_ctx *c){if(mode==1)return -ENOMEM;c->backend_state=malloc(1);assert(c->backend_state);live++;return mode==2?-EIO:0;}
static int fault_update(struct vxl_digest_ctx *c,const void *p,size_t n){(void)c;(void)p;(void)n;return mode==3?-EIO:0;}
static int fault_final(struct vxl_digest_ctx *c,vxl_u8 out[32]){(void)c;memset(out,0,32);return -EIO;}
static void fault_abort(struct vxl_digest_ctx *c){if(c->backend_state){free(c->backend_state);c->backend_state=NULL;live--;}}
static const struct vxl_digest_ops fault_ops={"fault-injection",VXL_DIGEST_PORTABLE,fault_begin,fault_update,fault_final,fault_abort};
const struct vxl_digest_ops *vxl_digest_select(enum vxl_digest_backend b){return mode?&fault_ops:((b==VXL_DIGEST_AUTO||b==VXL_DIGEST_PORTABLE)?vxl_digest_portable_ops():NULL);}
static void print_digest(const vxl_u8 out[32]){for(unsigned i=0;i<32;i++)printf("%02x",out[i]);puts("");}
int main(int argc,char **argv){
 struct vxl_digest_ctx c={0};vxl_u8 input[4097],out[32],again[32];unsigned i;
 for(i=0;i<sizeof(input);i++)input[i]=(vxl_u8)i;
 if(argc==3&&!strcmp(argv[1],"--file")){
  FILE *f=fopen(argv[2],"rb");size_t n;assert(f);assert(!vxl_digest_begin(&c,VXL_DIGEST_AUTO));
  while((n=fread(input,1,sizeof(input),f)))assert(!vxl_digest_update(&c,input,n));
  assert(!ferror(f));fclose(f);assert(!vxl_digest_final(&c,out));print_digest(out);return 0;
 }
 if(argc==2){unsigned n=(unsigned)strtoul(argv[1],NULL,10);assert(n<=sizeof(input));assert(!vxl_digest_begin(&c,VXL_DIGEST_PORTABLE));assert(!vxl_digest_update(&c,NULL,0));for(i=0;i<n;i+=7)assert(!vxl_digest_update(&c,input+i,n-i<7?n-i:7));assert(!vxl_digest_final(&c,out));assert(vxl_digest_final(&c,again)==-EINVAL);print_digest(out);return 0;}
 for(mode=1;mode<=4;mode++){
  memset(&c,0,sizeof(c));memset(out,0xa5,32);int rc=vxl_digest_begin(&c,VXL_DIGEST_AUTO);
  if(mode==1)assert(rc==-ENOMEM);
  else if(mode==2)assert(rc==-EIO);
  else {assert(!rc);rc=vxl_digest_update(&c,input,1);if(mode==3)assert(rc==-EIO);else{assert(!rc);assert(vxl_digest_final(&c,out)==-EIO);}}
  for(i=0;i<32;i++)assert(out[i]==0xa5);
  vxl_digest_abort(&c);vxl_digest_abort(&c);assert(live==0);assert(vxl_digest_update(&c,input,1)==-EINVAL);
 }
 mode=0;memset(&c,0,sizeof(c));assert(!vxl_digest_begin(&c,VXL_DIGEST_AUTO));assert(vxl_digest_begin(&c,VXL_DIGEST_AUTO)==-EBUSY);c.bytes=~(vxl_u64)0/8;assert(vxl_digest_update(&c,input,1)==-EOVERFLOW);vxl_digest_abort(&c);
 puts("DIGEST_FAULTS_NO_FAKE_OUTPUT_NO_LEAK_DOUBLE_FINAL_AND_OVERFLOW_PASS");return 0;
}
