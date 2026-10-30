#include <utility>
#include <vx_spawn.h>
#include <vx_tensor.h>
#include "hunet_ops.h"
#include "hunet_scalar_spans.h"
#include "hunet_ordered_reductions.h"

namespace vt=vortex::tensor;
using WMMA=vt::wmma_context<4,vt::fp16,vt::fp32>;
static_assert(WMMA::tileM==8 && WMMA::tileN==4 && WMMA::tileK==8,"frozen 4-thread WMMA contract");

static void scalar_body(HunetJobV1 * __UNIFORM__ j) {
    uint32_t t=blockIdx.x*blockDim.x+threadIdx.x;
    if(t<j->tasks)hunet_scalar_baseline(j,t);
}
#define HUNET_SPAN_BODY(name,span) \
static void name(HunetJobV1 * __UNIFORM__ j){ \
 uint32_t width=blockDim.x*hunet_span_chunk(j),first=blockIdx.x*width; \
 span(j,first+threadIdx.x,hunet_span_end(j,first,width),blockDim.x); \
}
HUNET_SPAN_BODY(gn_apply_body,hunet_gn_apply_span)
HUNET_SPAN_BODY(shift_body,hunet_shift_span)
HUNET_SPAN_BODY(add_body,hunet_add_span)
HUNET_SPAN_BODY(mul_body,hunet_mul_span)
HUNET_SPAN_BODY(slice_body,hunet_slice_span)
HUNET_SPAN_BODY(concat_body,hunet_concat_span)
HUNET_SPAN_BODY(copy_body,hunet_copy_span)
// Each fast path has its own entry so its register saves do not burden
// the original one-element callback (GN_APPLY, ADD, SHIFT, etc.).
static void pack_body(HunetJobV1 * __UNIFORM__ j) {
    hunet_pack_row(j,blockIdx.x,threadIdx.x,blockDim.x);
}
static void unpack_body(HunetJobV1 * __UNIFORM__ j) {
    hunet_unpack_row(j,blockIdx.x,threadIdx.x,blockDim.x);
}
static void gelu_body(HunetJobV1 * __UNIFORM__ j) {
    uint32_t chunk=j->tasks>=2048?32:1;
    uint32_t first=blockIdx.x*blockDim.x*chunk;
    uint32_t end=first+blockDim.x*chunk;if(end>j->tasks)end=j->tasks;
    hunet_gelu_span(j,first+threadIdx.x,end,blockDim.x);
}
static void gn_stats_body(HunetJobV1 * __UNIFORM__ j) {
    uint32_t t=blockIdx.x*blockDim.x+threadIdx.x;
    if(t<j->tasks)hunet_gn_stats_ordered(j);
}
static void gap_body(HunetJobV1 * __UNIFORM__ j) {
    uint32_t t=blockIdx.x*blockDim.x+threadIdx.x;
    if(t<j->tasks)hunet_gap_ordered(j,t);
}
static void gemm_body(HunetJobV1 * __UNIFORM__ j) {
    const auto *m=(const HunetMatrix*)(uintptr_t)j->aux;
    const auto *w=(const uint16_t*)(uintptr_t)j->w,*a=(const uint16_t*)(uintptr_t)j->x;
    auto *y=(float*)(uintptr_t)j->y;
    uint32_t tile=blockIdx.x,row=(tile/(m->NP/4))*8,col=(tile%(m->NP/4))*4;
    WMMA::fragment_a fw;WMMA::fragment_b fa;WMMA::fragment_acc result;
    WMMA::fill_fragment(result,0.0f);
    for(uint32_t k=0;k<m->KP;k+=WMMA::tileK) {
        WMMA::load_matrix_sync(fw,w+row*m->KP+k,m->KP);
        WMMA::load_matrix_sync(fa,a+k*m->NP+col,m->NP);
        WMMA::mma_sync(result,fw,fa,result);
    }
    WMMA::store_matrix_sync(y+row*m->NP+col,result,m->NP);
    if(vx_thread_id()==0 && j->scratch) ((uint32_t*)(uintptr_t)j->scratch)[tile]=vx_core_id()+1;
}

int main() {
    auto *job=(HunetJobV1*)(uintptr_t)csr_read(VX_CSR_MSCRATCH);
    if(!hunet_valid_job(job) || job->tasks==0)return 1;
    uint32_t block=4,grid;
    const auto*m=(const HunetMatrix*)(uintptr_t)job->aux;
    if(job->op==HUNET_GEMM)grid=job->tasks;
    else if(job->op==HUNET_PACK)grid=m->KP;
    else if(job->op==HUNET_UNPACK)grid=m->M;
    else if(job->op==HUNET_GELU){uint32_t span=4*(job->tasks>=2048?32:1);grid=(job->tasks+span-1)/span;}
    else if(hunet_is_span_op(job->op)){uint32_t span=4*hunet_span_chunk(job);grid=(job->tasks+span-1)/span;}
    else grid=(job->tasks+3)/4;
    vx_kernel_func_cb body=(vx_kernel_func_cb)scalar_body;
    if(job->op==HUNET_GEMM)body=(vx_kernel_func_cb)gemm_body;
    else if(job->op==HUNET_PACK)body=(vx_kernel_func_cb)pack_body;
    else if(job->op==HUNET_UNPACK)body=(vx_kernel_func_cb)unpack_body;
    else if(job->op==HUNET_GELU)body=(vx_kernel_func_cb)gelu_body;
    else if(job->op==HUNET_GN_STATS)body=(vx_kernel_func_cb)gn_stats_body;
    else if(job->op==HUNET_GAP)body=(vx_kernel_func_cb)gap_body;
    else if(job->op==HUNET_GN_APPLY)body=(vx_kernel_func_cb)gn_apply_body;
    else if(job->op==HUNET_SHIFT)body=(vx_kernel_func_cb)shift_body;
    else if(job->op==HUNET_ADD)body=(vx_kernel_func_cb)add_body;
    else if(job->op==HUNET_MUL_CHANNEL)body=(vx_kernel_func_cb)mul_body;
    else if(job->op==HUNET_SLICE)body=(vx_kernel_func_cb)slice_body;
    else if(job->op==HUNET_CONCAT)body=(vx_kernel_func_cb)concat_body;
    else if(job->op==HUNET_COPY)body=(vx_kernel_func_cb)copy_body;
    int rc=vx_spawn_threads(1,&grid,&block,body,job);
    // Every participating core completes its writes before the job terminates.
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    return rc;
}
