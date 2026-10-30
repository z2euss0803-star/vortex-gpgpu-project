#include <stddef.h>
#include <stdio.h>
#include <vxl/s8_wire.h>
#include <uapi/vx_linux.h>
#define OFF(t,f,n) _Static_assert(offsetof(struct t,f)==n,"offset:" #f)
_Static_assert(sizeof(struct vxl_request)==176,"legacy envelope");
OFF(vxl_request,handle,16); OFF(vxl_request,handle2,24); OFF(vxl_request,offset,32);
OFF(vxl_request,offset2,40); OFF(vxl_request,length,48); OFF(vxl_request,user_ptr,56);
OFF(vxl_request,job_id,64); OFF(vxl_request,deadline_ms,72); OFF(vxl_request,wait_ms,80);
OFF(vxl_request,address,88); OFF(vxl_request,capacity,96); OFF(vxl_request,used,104);
OFF(vxl_request,domain,112); OFF(vxl_request,purpose,116); OFF(vxl_request,gpu_address,120);
OFF(vxl_request,state,124); OFF(vxl_request,result,128); OFF(vxl_request,generation,132);
OFF(vxl_request,profile_id,136); OFF(vxl_request,capability,140); OFF(vxl_request,alignment,144);
OFF(vxl_request,hwstatus,148); OFF(vxl_request,errors,152); OFF(vxl_request,pending_w,156);
OFF(vxl_request,pending_r,160); OFF(vxl_request,jobs,164); OFF(vxl_request,trust_id,168);
OFF(vxl_request,reserved2,172);
_Static_assert(VXL_ABI_MAJOR==1,"legacy ABI");
#define NR(c,n) _Static_assert(_IOC_NR(c)==n && _IOC_SIZE(c)==176,"command " #c)
NR(VXL_GET_INFO,1);NR(VXL_ALLOC,2);NR(VXL_RESERVE,3);NR(VXL_FREE,4);NR(VXL_COPY_TO,5);
NR(VXL_COPY_FROM,6);NR(VXL_COPY_DEVICE,7);NR(VXL_SUBMIT,8);NR(VXL_WAIT,9);NR(VXL_GET_STATUS,10);
NR(VXL_REAP,11);NR(VXL_HUNET_SEAL,12);NR(VXL_HUNET_BEGIN_ROI,13);NR(VXL_HUNET_END_ROI,14);
NR(VXL_HUNET_GET_EVIDENCE,15);NR(VXL_HUNET_GET_BINDING,16);
NR(VXL_HYBRID_SEAL,17);NR(VXL_HYBRID_CPU_BEGIN,18);NR(VXL_HYBRID_CPU_COMMIT,19);
NR(VXL_HYBRID_GET_CPU_EVIDENCE,20);NR(VXL_HYBRID_END,21);NR(VXL_HYBRID_GET_BINDING,22);
#include "s8_contract_generated.h"
_Static_assert(sizeof(struct vxl_s8_job_v1)==128,"S8 job");
_Static_assert(sizeof(struct vxl_s8_result_v1)==256,"S8 metadata");
_Static_assert(sizeof(struct vxl_s8_binding_v1)==512,"S8 binding");
OFF(vxl_s8_job_v1,run_id,72);OFF(vxl_s8_job_v1,binding_tag,88);OFF(vxl_s8_job_v1,args_crc32,104);
OFF(vxl_s8_result_v1,done,120);OFF(vxl_s8_result_v1,reserved,128);
int main(void) { puts("S8_WIRE_AND_LEGACY_ABI_PASS");return 0; }
