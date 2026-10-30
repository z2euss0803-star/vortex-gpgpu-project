#include <vx_spawn.h>
#include <vx_tensor.h>
#include <stdint.h>
#include "hunet_job.h"
namespace vt=vortex::tensor;
using WMMA=vt::wmma_context<4,vt::fp16,vt::fp32>;
static_assert(WMMA::tileM==8&&WMMA::tileN==4&&WMMA::tileK==8,"original WMMA");
static void gemm(HunetJobV1 * __UNIFORM__ j) {
 const HunetMatrix *m=(const HunetMatrix*)(uintptr_t)j->aux;
 const uint16_t *w=(const uint16_t*)(uintptr_t)j->w,*a=(const uint16_t*)(uintptr_t)j->x;
 float *y=(float*)(uintptr_t)j->y;
 uint32_t tile=blockIdx.x,row=(tile/(m->NP/4))*8,col=(tile%(m->NP/4))*4;
 WMMA::fragment_a fw;WMMA::fragment_b fa;WMMA::fragment_acc r;
 WMMA::fill_fragment(r,0.0f);
 for(uint32_t k=0;k<m->KP;k+=8) {
  WMMA::load_matrix_sync(fw,w+row*m->KP+k,m->KP);
  WMMA::load_matrix_sync(fa,a+k*m->NP+col,m->NP);
  WMMA::mma_sync(r,fw,fa,r);
 }
 WMMA::store_matrix_sync(y+row*m->NP+col,r,m->NP);
 if(vx_thread_id()==0)((uint32_t*)(uintptr_t)j->scratch)[tile]=vx_core_id()+1;
}
int main() {
 HunetJobV1 *j=(HunetJobV1*)(uintptr_t)csr_read(VX_CSR_MSCRATCH);
 if(!hunet_valid_job(j)||j->op!=HUNET_GEMM||(j->tasks!=1&&j->tasks!=16))return 1;
 uint32_t grid=j->tasks,block=4;
 int rc=vx_spawn_threads(1,&grid,&block,(vx_kernel_func_cb)gemm,j);
 __asm__ volatile("fence iorw, iorw":::"memory");return rc;
}
