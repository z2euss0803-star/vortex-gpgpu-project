#ifndef S8_PHASE_H
#define S8_PHASE_H
#include <stdint.h>
#include <vxl/s8_wire.h>
#define S8_PHASE_MAGIC 0x31503853u
struct s8_cycle_io {void *context;uint32_t (*high)(void *);uint32_t (*low)(void *);};
int s8_read_mcycle_from(const s8_cycle_io *,uint64_t *);
int s8_read_mcycle(uint64_t *);
int s8_phase_init(volatile vxl_s8_phase_v1 *,uint32_t,uint32_t);
int s8_phase_store(volatile vxl_s8_phase_v1 *,uint32_t,uint64_t);
#if defined(__riscv)
int s8_phase_open(const vxl_s8_job_v1 *,uint32_t,uint32_t);
int s8_phase_event(uint32_t,uint64_t);
int s8_phase_mark(uint32_t);
#endif
#endif
