#ifndef S8_DEVICE_PROTOCOL_H
#define S8_DEVICE_PROTOCOL_H
#include <stdint.h>
#include <vxl/s8_runtime.h>
struct s8_words4 {uint32_t word[4];};
/* Only instruction/fence boundaries are replaceable for native software tests.
 * participant=4 is TEST_ONLY aggregate storage, device uses its actual lane. */
struct s8_protocol_io {
 void *context;
 s8_words4 (*begin)(void *,const s8_words4 &);
 int (*step)(void *,const s8_words4 &,const s8_words4 &);
 s8_words4 (*read)(void *,const s8_words4 &);
 void (*fence)(void *);
 uint32_t participant;
 int (*phase)(void *,uint32_t)=nullptr;
};
struct s8_protocol_state {
 const vxl_s8_job_v1 *job;const s8_protocol_io *io;
 uint8_t *output;vxl_s8_result_v1 meta;
};
int s8_state_init(s8_protocol_state *,const vxl_s8_job_v1 *,uint32_t core,uint32_t warp,uint8_t *out,uint32_t bytes,const s8_protocol_io *);
bool s8_same(const s8_words4 &,const s8_words4 &);
int s8_record(s8_protocol_state *,uint32_t event,uint32_t index,const s8_words4 &,uint32_t mask=15);
int s8_publish(s8_protocol_state *,int result,uint32_t event);
int s8_execute_positive(const vxl_s8_job_v1 *,uint32_t core,uint32_t warp,const int16_t *a,uint32_t a_bytes,const int16_t *b,uint32_t b_bytes,uint8_t *out,uint32_t out_bytes,const s8_protocol_io *);
int s8_run_positive(const vxl_s8_job_v1 *,uint32_t core,uint32_t warp);
#if defined(__riscv)
s8_protocol_io s8_native_io();
#endif
#endif
