#ifndef VXL_S8_TRACE_H
#define VXL_S8_TRACE_H
#include <vxl/s8.h>
#define VXL_S8_JOB_CAPACITY 256u
#define VXL_S8_OP_CAPACITY 2048u
#define VXL_S8_TRACE_VERSION 1u
/* binding.bin is exactly the private 512B binding. CSV version headers carry
 * the epoch/frontier; export admission for all three files binds that frontier. */
struct vxl_s8_job_row {
 struct vxl_s8_job_v1 args;
 struct vxl_s8_hw_snapshot hw;
 struct vxl_s8_timing timing;
 vxl_u64 job_id;
 vxl_u32 state,error_class;
 int result;
};
struct vxl_s8_op_row {
 vxl_u64 job_id,begin_ns,end_ns,bytes;
 vxl_u32 generation,sequence,metric,valid;
 int result;
};
struct vxl_s8_trace {
 struct vxl_s8_job_row *jobs;
 struct vxl_s8_op_row *operations;
 vxl_u32 jobs_used,operations_used,active,failed,overflow,abandoned,generation;
 vxl_u8 boot[16];
 vxl_u64 revision,exported[3],serial,grant[3],grant_bytes[3];
};
struct vxl_s8_reader {
 const struct vxl_s8_trace *origin;
 vxl_u64 revision,serial,bytes,delivered;
 vxl_u32 generation,file,broken,finished;
};
typedef int (*vxl_s8_emit_fn)(void *,const char *,size_t);
#ifdef __cplusplus
extern "C" {
#endif
int vxl_s8_trace_init(struct vxl_s8_ctx *,struct vxl_s8_trace *,struct vxl_s8_job_row *,struct vxl_s8_op_row *,const vxl_u8 boot[16]);
int vxl_s8_trace_append(struct vxl_s8_ctx *,vxl_u32 table,const void *,vxl_u32 bytes);
int vxl_s8_trace_begin(struct vxl_s8_ctx *,const struct vxl_s8_job_v1 *,vxl_u64 job_id);
void vxl_s8_trace_sync(struct vxl_s8_ctx *);
int vxl_s8_trace_open_allowed(const struct vxl_s8_ctx *);
void vxl_s8_trace_new_epoch(struct vxl_s8_ctx *);
void vxl_s8_trace_close(struct vxl_s8_ctx *);
int vxl_s8_trace_emit(const struct vxl_s8_trace *,const struct vxl_s8_program *,vxl_u32 file,vxl_s8_emit_fn,void *);
int vxl_s8_reader_begin(struct vxl_s8_ctx *,vxl_u32 file,vxl_u64 bytes,struct vxl_s8_reader *);
int vxl_s8_reader_advance(struct vxl_s8_ctx *,struct vxl_s8_reader *,vxl_u64 offset,vxl_u64 bytes,int copy_ok);
int vxl_s8_reader_finish(struct vxl_s8_ctx *,struct vxl_s8_reader *);
int vxl_s8_export_complete(struct vxl_s8_ctx *,vxl_u32 file,vxl_u64 delivered);
#ifdef __cplusplus
}
#endif
#endif
