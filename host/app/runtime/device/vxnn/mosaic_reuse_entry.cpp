#include "mosaic_wire_reuse_v4.h"
#include "mosaic_coop.h"
#include "mosaic_profile_ops.h"
#include <vx_intrinsics.h>
#include <vx_spawn.h>
#include <errno.h>
#include <string.h>
static __thread int math_errno;
extern "C" int*__errno(){return &math_errno;}
namespace {
void fence(){__asm__ volatile("fence iorw, iorw":::"memory");}
bool rounding_ok(){uint32_t f;__asm__ volatile("csrr %0, fcsr":"=r"(f));return !(f&0xe0u);}
template<class T>T*pointer(uint32_t address){return reinterpret_cast<T*>(uintptr_t(address));}
void callback(void*opaque){
 const auto*a=static_cast<const VxnnMosaicArgs*>(opaque);const uint32_t core=vx_core_id(),warp=vx_warp_id(),lane=vx_thread_id();
 if(warp)return;
 auto io=mosaic_native_s8_ops(core,warp,lane,65536);
 auto*abort=pointer<volatile uint32_t>(0x10002000u+core*256+64);
 if(lane==0)*abort=0;fence();vx_barrier(0,1);
 VxnnMosaicShape shape{};MosaicWorkLayoutV1 layout{};
 int rc=core>3||lane>3||blockIdx.x!=core||threadIdx.x!=lane||vx_hart_id()!=core*16+lane||!rounding_ok()?-EINVAL:vxnn_mosaic_validate_reuse_v4(a,true,&shape);
 if(!rc)rc=mosaic_reuse_work_layout_v4(a->opcode,a->kpad,shape.norm_elements,&layout);
 rc=io.agree(io.context,rc);MosaicProfileRecordV1*profile=nullptr;
 if(!rc){
  const uint32_t work=a->address[VXM_WORK]+64+core*layout.core_stride;
  profile=pointer<MosaicProfileRecordV1>(work+layout.profile_offset);
  if(!lane)mosaic_profile_initialize(profile,*a,layout,core);
  rc=io.agree(io.context,io.synchronize(io.context));
  io.profile=a->reserved?profile:nullptr;
  VxnnMosaicPrimitive primitive{a->opcode,a->first,a->count,{a->ci,a->co,a->h,a->w,a->k,a->stride,a->upsample,a->kpad},0.f,bool(a->flags&1),bool(a->flags&2)};
  primitive.schedule_version=2;primitive.weight_layout_version=2;
  memcpy(&primitive.scale,&a->scale_bits,4);VxnnMosaicBuffers b;
  if(shape.used_mask&(1u<<VXM_INPUT))b.input={pointer<const float>(a->address[VXM_INPUT]),a->extent[VXM_INPUT]/4};
  if(shape.used_mask&(1u<<VXM_OUTPUT))b.output={pointer<float>(a->address[VXM_OUTPUT]),a->extent[VXM_OUTPUT]/4};
  if(shape.used_mask&(1u<<VXM_QUANT))b.quant={pointer<int16_t>(a->address[VXM_QUANT]),a->extent[VXM_QUANT]/2};
  if(shape.used_mask&(1u<<VXM_WEIGHTS))b.weights={pointer<const int16_t>(a->address[VXM_WEIGHTS]),a->extent[VXM_WEIGHTS]/2};
  if(a->opcode==2)b.coeff={pointer<const float>(a->address[VXM_COEFF]),a->extent[VXM_COEFF]/4};
  if(a->opcode==4||a->opcode>=7){const float*coef=pointer<const float>(a->address[VXM_COEFF]);b.gamma={coef+a->co*2,a->co};b.beta={coef+a->co*3,a->co};}
  if(shape.used_mask&(1u<<VXM_STATS))b.stats={pointer<float>(a->address[VXM_STATS]),a->extent[VXM_STATS]/4};
  if(shape.used_mask&(1u<<VXM_SKIP))b.skip={pointer<float>(a->address[VXM_SKIP]),a->extent[VXM_SKIP]/4};
  if(a->opcode==2){b.a={pointer<int16_t>(work+layout.a_offset),size_t(a->kpad)*8};b.b={pointer<int16_t>(work+layout.b_offset),size_t(a->kpad)*8};b.acc={pointer<int64_t>(work+layout.acc_offset),64};io.raw_pages=pointer<uint32_t>(work+layout.raw_offset);}
  if(a->opcode==3)b.norm={pointer<float>(work),pointer<float>(work+shape.norm_elements*4),shape.norm_elements};
  if(!rc)rc=vxnn_mosaic_coop_primitive(&primitive,&b,core,&io);
 }
 if(!rounding_ok()&&!rc)rc=-EDOM;rc=io.agree(io.context,rc);
 if(lane==0){if(profile){profile->header.error_code=rc;profile->header.status=rc?MOSAIC_PROFILE_FAILED_V1:MOSAIC_PROFILE_COMPLETE_V1;}if(rc)*abort=1;}
 fence();vx_barrier(0,1);fence();vx_barrier(0,1);
}
}
int main(){
 const uint32_t original=csr_read(VX_CSR_MSCRATCH);
 if(original!=0x80100040u||vx_num_cores()!=4||vx_num_warps()!=4||vx_num_threads()!=4||!rounding_ok())return 1;
 VxnnMosaicArgs args=*pointer<const VxnnMosaicArgs>(original);VxnnMosaicShape shape{};
 if(vxnn_mosaic_validate_reuse_v4(&args,true,&shape))return 1;
 uint32_t grid=4,block=16;const int rc=vx_spawn_threads(1,&grid,&block,callback,&args);fence();
 return rc?rc:(*pointer<volatile uint32_t>(0x10002000u+vx_core_id()*256+64)?1:0);
}
