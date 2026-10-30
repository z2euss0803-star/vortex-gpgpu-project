#ifndef S8_DEVICE_NEGATIVE_H
#define S8_DEVICE_NEGATIVE_H
#include "protocol.h"
struct s8_negative_io {
 s8_protocol_io instruction;
 int (*contender)(void *,unsigned phase,s8_protocol_state *,const int16_t *,const int16_t *);
 s8_words4 (*partial_begin)(void *,const s8_words4 &,volatile vxl_s8_status_v1 *);
};
uint32_t s8_negative_status_count(uint32_t case_id);
int s8_execute_peer_phase(s8_protocol_state *,unsigned phase,const int16_t *,const int16_t *);
int s8_execute_negative(const vxl_s8_job_v1 *,uint32_t core,uint32_t warp,const int16_t *,uint32_t a_bytes,const int16_t *,uint32_t b_bytes,uint8_t *,uint32_t out_bytes,const s8_negative_io *);
int s8_run_negative(const vxl_s8_job_v1 *,uint32_t core,uint32_t warp);
#endif
