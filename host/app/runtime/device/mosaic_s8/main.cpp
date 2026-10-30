#include "entry.h"
#include "launch.h"
#include <vx_intrinsics.h>
#include <vx_spawn.h>
#include <mosaic_device_catalog.h>
#include <new>
// A fresh CRT clears all TLS and all per-hart FCSR on every bounded job.
alignas(mosaic_device_context) static __thread unsigned char context_storage[sizeof(mosaic_device_context)];
static __thread int callback_error;
// The frozen minimal libc has no errno accessor. libm expects newlib's
// per-thread accessor; provide real hart-local storage, not a shared stub.
static __thread int math_errno;
extern "C" int *__errno(){return &math_errno;}
static void fence(){__asm__ volatile("fence iorw, iorw":::"memory");}
static void callback(void *argument){
 const auto *job=static_cast<const mosaic_job_v1 *>(argument);
 uint32_t core=vx_core_id(),warp=vx_warp_id(),lane=vx_thread_id();
 if(!mosaic_owner_warp(warp))return;
 auto *abort=reinterpret_cast<volatile uint32_t *>(uintptr_t(0x10002800+core*64+60));
 if(lane==0)*abort=0;
 fence();vx_barrier(0,1);
 __asm__ volatile("csrw fcsr, zero":::"memory");uint32_t fcsr;__asm__ volatile("csrr %0, fcsr":"=r"(fcsr));
 int rc=(fcsr||warp||vx_hart_id()!=core*16+lane||blockIdx.x!=core||threadIdx.x!=lane)?-EINVAL:0;
 auto *context=new (context_storage) mosaic_device_context{};
 if(!rc)rc=mosaic_entry_prepare(job,mosaic_device_catalog,mosaic_device_catalog_count,core,context);
 if(!rc){
  context->io[core]=mosaic_native_s8_ops(core,warp,lane,65536);
  rc=mosaic_execute_job(job,context);
 }
 __asm__ volatile("csrr %0, fcsr":"=r"(fcsr));if(fcsr&0xe0u)rc=-EDOM;
 if(rc)*abort=1;
 fence();vx_barrier(0,1);callback_error=*abort?1:0;
 if(lane==0&&!callback_error){
  unsigned char row[64];rc=mosaic_entry_metadata(job,core,row);
  auto *destination=reinterpret_cast<volatile unsigned char *>(uintptr_t(0x10002800+core*64));
  if(rc)callback_error=1;
  else for(unsigned i=0;i<64;i++)destination[i]=row[i];
 }
 fence();vx_barrier(0,1);
}
int main(){
 uint32_t original=csr_read(VX_CSR_MSCRATCH);
 if(original!=0x80100040u||vx_num_cores()!=4||vx_num_warps()!=4||vx_num_threads()!=4)return 1;
 // Copy before SDK scheduling repurposes MSCRATCH, and retain it on this core's
 // primary stack until all four lanes of its sole owner warp have returned.
 mosaic_job_v1 job=*reinterpret_cast<const mosaic_job_v1 *>(uintptr_t(original));
 uint32_t grid=MOSAIC_LAUNCH_GRID,block=MOSAIC_LAUNCH_BLOCK;callback_error=0;
 int rc=vx_spawn_threads(1,&grid,&block,callback,&job);fence();
 return rc?rc:callback_error;
}
