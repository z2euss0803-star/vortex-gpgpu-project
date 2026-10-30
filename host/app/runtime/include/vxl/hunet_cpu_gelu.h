#ifndef VXL_HUNET_CPU_GELU_H
#define VXL_HUNET_CPU_GELU_H
#include <stddef.h>
#include <stdint.h>
struct hunet_cpu_timing {uint64_t begin_ns,end_ns,duration_ns;uint32_t threads,reserved;};
#ifdef __cplusplus
extern "C" {
#endif
/* Result buffer and timing are published only after all four workers join. */
int hunet_cpu_gelu4(const float *,float *,size_t,struct hunet_cpu_timing *);
#ifdef __cplusplus
}
#endif
#endif
