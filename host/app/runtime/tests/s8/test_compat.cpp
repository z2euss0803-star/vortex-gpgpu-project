/* CPU boundary fixture. No GPU, WMMA or TEX hardware execution. */
#include "../../device/s8/compat.h"
#include "../../tools/s8/reference.h"
#include <cassert>
#include <cstring>
#include <cstdio>
static int half(uint16_t x){assert(x==0||x==0x3c00||x==0xbc00);return x==0?0:x==0x3c00?1:-1;}
static int wmma(void *,const uint16_t *a,const uint16_t *b,volatile float *out){for(unsigned i=0;i<8;++i)for(unsigned j=0;j<4;++j){int sum=0;for(unsigned k=0;k<8;++k)sum+=half(a[i*8+k])*half(b[k*4+j]);out[i*4+j]=float(sum);}return 0;}
static int tex(void *,const uint32_t *coords,unsigned count,volatile uint32_t *out){for(unsigned i=0;i<count;++i){unsigned x=coords[i*2],y=coords[i*2+1];unsigned value=x==32768?6:(x?4:0)+(y?8:0);out[i]=value*0x01010101u;}return 0;}
static void fence(void *){}
int main(){
 for(unsigned id=2;id<=5;++id){vxl_s8_job_v1 j={};j.magic=VXL_S8_JOB_MAGIC;j.version=1;j.bytes=128;j.case_id=id;j.core_mask=1;j.k=id==3?8:0;j.generation=2;j.sequence=1;
 alignas(16) uint8_t a[256]={0},b[256]={0},out[4096];unsigned ab=0,bb=0;assert(!s8_compat_inputs(id,a,sizeof(a),&ab,b,sizeof(b),&bb));
 memset(out,0xcc,sizeof(out));memset(out+0x3c0,0,256);s8_compat_io io={nullptr,wmma,tex,fence,4};
 assert(!s8_execute_compat(&j,0,0,a,ab,b,bb,out,sizeof(out),&io));unsigned n=s8_compat_bytes(id);assert(!s8_compare_compat(id,out+64,n));
 for(unsigned k=64+n;k<448;++k)assert(out[k]==0xcc);
 for(unsigned k=0x4c0;k<0x6c0;++k)assert(out[k]==0xcc);
 for(unsigned k=0x900;k<0xc80;++k)assert(out[k]==0xcc);
 auto *m=reinterpret_cast<vxl_s8_result_v1 *>(out+0x3c0);assert(m->done==VXL_S8_DONE&&!m->sw_status&&!m->pages_count&&!m->status_count&&!m->recovery_elements);
 out[64]^=1;assert(s8_compare_compat(id,out+64,n)<0);assert(s8_compare_compat(id,out+64,n-1)<0);
 assert(s8_execute_compat(&j,0,0,a,ab-1,b,bb,out,sizeof(out),&io)<0);
 }
 puts("S8_COMPAT_EXACT_PAYLOAD_FORMAT_TAIL_AND_APPROVED_TEX_MODES_PASS SOFTWARE_BOUNDARY_ONLY");
}
