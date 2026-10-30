#ifndef VXL_DEMO_TIMING_H
#define VXL_DEMO_TIMING_H
#include <vxl/demo_runner.h>
enum demo_phase {DEMO_DECODE,DEMO_PREPROCESS,DEMO_LOAD,DEMO_QUANT_PACK,DEMO_GPU_SUBMIT_WAIT,DEMO_GUARD_CDMA,DEMO_POLL,DEMO_HANDOFF,DEMO_CPU_POST,DEMO_READBACK,DEMO_COLOUR,DEMO_DISPLAY_SUBMIT,DEMO_DISPLAY_PRESENT,DEMO_PHASE_COUNT};
enum demo_clock {DEMO_A53_MONOTONIC=1,DEMO_B_MONOTONIC,DEMO_DRM_UNKNOWN};
struct demo_interval {enum demo_phase phase;uint64_t begin,end;enum demo_clock clock;int valid,parent;};
struct demo_cycles {uint32_t raw,generation;uint64_t job_id;int valid,pre_ack,submit_enter,wrapped;};
struct demo_timeline {int real_board;uint32_t generation;struct demo_interval intervals[2048];size_t count;struct demo_cycles cycles[2048];size_t cycle_count;};
struct demo_error {const char *run_id,*error_id,*command,*source_sha,*input_sha,*message;int natural_exit;uint32_t gpu_raw;int raw_observed,cycle_valid,real_board;};
#ifdef __cplusplus
extern "C" {
#endif
int demo_time_record(struct demo_timeline *,enum demo_phase,uint64_t,uint64_t,enum demo_clock,int);
int demo_time_record_child(struct demo_timeline *,enum demo_phase,uint64_t,uint64_t,enum demo_clock,int,int parent);
int demo_gpu_cycles_record(struct demo_timeline *,uint32_t,int,uint32_t,uint64_t,int);
int demo_gpu_cycles_capture(struct demo_timeline *,uint32_t,int,uint32_t,uint64_t,int,int submit_enter,int wrapped);
int demo_timeline_write(const struct demo_timeline *,char *,size_t,size_t *);
int demo_error_save(struct demo_store *,const struct demo_error *);
#ifdef __cplusplus
}
#endif
#endif
