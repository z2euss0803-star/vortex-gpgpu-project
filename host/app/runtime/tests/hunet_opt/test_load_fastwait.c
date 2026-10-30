#include <assert.h>
#include <stdio.h>
#include <vxl/engine.h>
#include <vxl/hunet.h>
#include <vxl/hunet_opt.h>
int main(void){
 struct vxl_engine e={0};struct vxl_hunet_ctx c={0};struct vxl_opt_release r={0};
 struct vxl_opt_catalog cat={.runtime_flags=1};r.catalog=&cat;
 c.engine=&e;c.opt.release=&r;c.program_id=6;c.opt_load_locked=1;c.opt_load_owner=7;c.opt_load_generation=3;
 e.owner=7;e.generation=3;e.profile_id=4;e.opened=1;e.job.state=VXL_READY;
 r.wire_bytes=8192;r.binding.v3.v2.v1.magic=VXL_HUNET_BINDING_MAGIC;r.binding.v3.v2.v1.version=4;r.binding.v3.v2.v1.program_id=10;r.binding.v3.v2.v1.features=31;r.binding.v3.v2.v1.bytes=512;r.binding.v3.v2.v1.profile_id=4;
 e.objects[0]=(struct vxl_object){.handle=0x300000001ULL,.domain=VXL_PL,.purpose=VXL_CODE,.gpu=VXL_HUNET_CODE,.length=8388608};
 e.objects[1]=(struct vxl_object){.handle=0x300000002ULL,.domain=VXL_PL,.purpose=VXL_WEIGHTS,.gpu=VXL_HUNET_WEIGHTS,.length=67108864};
 vxl_u64 h=e.objects[0].handle;
 assert(vxl_opt_load_fastwait_allowed(&c,VXL_COPY_TO,h,0,4096));
 assert(vxl_opt_load_fastwait_allowed(&c,VXL_COPY_TO,e.objects[1].handle,0,23685056));
 assert(vxl_opt_load_fastwait_allowed(&c,VXL_HYBRID_SEAL,0,0,0));
 assert(!vxl_opt_load_fastwait_allowed(&c,VXL_COPY_FROM,h,0,4096));
 assert(!vxl_opt_load_fastwait_allowed(&c,VXL_COPY_TO,h,8388600,4096));
 assert(!vxl_opt_load_fastwait_allowed(&c,VXL_COPY_TO,h,~(vxl_u64)0,1));
 assert(!vxl_opt_load_fastwait_allowed(&c,VXL_COPY_TO,h,0,0));
 c.opt_load_locked=0;assert(!vxl_opt_load_fastwait_allowed(&c,VXL_COPY_TO,h,0,16));c.opt_load_locked=1;
 c.opt_load_owner=8;assert(!vxl_opt_load_fastwait_allowed(&c,VXL_COPY_TO,h,0,16));c.opt_load_owner=7;
 c.opt_load_generation=4;assert(!vxl_opt_load_fastwait_allowed(&c,VXL_COPY_TO,h,0,16));c.opt_load_generation=3;
 c.active=1;assert(!vxl_opt_load_fastwait_allowed(&c,VXL_HYBRID_SEAL,0,0,0));c.active=0;
 c.hybrid.enabled=1;c.hybrid.phase=VXL_HYBRID_CPU_WAIT;assert(!vxl_opt_load_fastwait_allowed(&c,VXL_HYBRID_SEAL,0,0,0));c.hybrid.phase=0;
 e.objects[0].purpose=VXL_DATA;assert(!vxl_opt_load_fastwait_allowed(&c,VXL_COPY_TO,h,0,16));e.objects[0].purpose=VXL_CODE;
 e.objects[0].gpu+=64;assert(!vxl_opt_load_fastwait_allowed(&c,VXL_COPY_TO,h,0,16));e.objects[0].gpu-=64;
 e.profile_id=1;assert(!vxl_opt_load_fastwait_allowed(&c,VXL_COPY_TO,h,0,16));e.profile_id=4;
 r.binding.v3.v2.v1.program_id=9;assert(!vxl_opt_load_fastwait_allowed(&c,VXL_COPY_TO,h,0,16));r.binding.v3.v2.v1.program_id=10;
 c.sealed=1;assert(!vxl_opt_load_fastwait_allowed(&c,VXL_COPY_TO,h,0,16));c.sealed=0;
 e.opened=0;assert(!vxl_opt_load_fastwait_allowed(&c,VXL_COPY_TO,h,0,16));e.opened=1;
 e.quarantined=1;assert(!vxl_opt_load_fastwait_allowed(&c,VXL_HYBRID_SEAL,0,0,0));
 puts("PRESEAL_PROBE_BOUND_FASTWAIT_SCOPE_MATRIX_PASS_NOT_DMA_SPEED");return 0;
}
