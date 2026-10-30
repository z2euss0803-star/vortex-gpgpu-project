/* Actual production early prepare path, deliberately stopped at first guard.
 * No CDMA/START/numerics claim. Namespaced symbols cannot replace real driver
 * functions linked by the allocator fixture in the same test executable. */
#include <vxl/v2/execution.h>
#define vxl_v2_ref_resolve mosaic_alias_resolve
#define vxl_v2_hw_idle mosaic_alias_idle
#define vxl_v2_native_guards mosaic_alias_guards
#define vxl_v2_prepare_tex mosaic_alias_tex
#define vxl_v2_execution_bind mosaic_alias_bind
#define vxl_v2_execution_snapshot mosaic_alias_snapshot
#define vxl_v2_native_execution_ops mosaic_alias_ops
static unsigned alias_guard_calls;
static struct vxl_v2_object alias_private_args;
int mosaic_alias_resolve(struct vxl_v2_ctx*c,vxl_u64 owner,const struct vxl_v2_ref*r,vxl_u32 access,struct vxl_v2_object**out){(void)c;(void)owner;(void)r;(void)access;*out=&alias_private_args;return 0;}
int mosaic_alias_idle(struct vxl_v2_hw*h){(void)h;return 0;}
int mosaic_alias_guards(struct vxl_v2_hw*h,struct vxl_v2_object*o,int mode){(void)h;(void)o;(void)mode;++alias_guard_calls;return -EAGAIN;}
int mosaic_alias_tex(struct vxl_v2_hw*h,const struct vxl_v2_job*j,const struct vxl_v2_tex*t){(void)h;(void)j;(void)t;return -EOPNOTSUPP;}
#include "driver/v2/execution.c"
static vxl_u64 alias_clock(void*p){(void)p;return 100;}
int mosaic_prepare_alias_probe(const vxl_u64 handles[8],const vxl_u64 offsets[8],const vxl_u64 lengths[8],const vxl_u32 access[8],unsigned*guard_calls){
 struct vxl_v2_execution x={0};struct vxl_v2_ctx ctx={0};struct vxl_v2_hw hw={0};struct vxl_v2_program program={0};struct vxl_v2_job job={0};struct vxl_v2_program_table table={0};vxl_u8 schema[64]={0},args[128]={0};
 ctx.owner=1;ctx.objects[0].handle=1;ctx.objects[0].guard_bytes=64;ctx.programs=&table;table.catalog.bytes=schema;
 hw.ctx=&ctx;hw.now_ns=alias_clock;x.hw=&hw;x.count=1;x.policies[0].reserved[0]=VXL_V2_RETIRE_GLOBAL_DRAIN;x.policies[0].directory_ref=VXL_V2_ABSENT;
 program.entry.args_bytes=128;program.entry.args_gpu=0x80010000;program.owned_count=1;program.owned[0].handle=1;alias_private_args.gpu=program.entry.args_gpu;
 job.num_refs=8;for(unsigned i=0;i<8;i++){job.refs[i].handle=handles[i];job.refs[i].object_generation=1;job.refs[i].device_generation=1;job.refs[i].offset=offsets[i];job.refs[i].length=lengths[i];job.refs[i].access=access[i];}
 alias_guard_calls=0;int rc=prepare_execution(&x,&program,&job,args,128);*guard_calls=alias_guard_calls;return rc;
}
