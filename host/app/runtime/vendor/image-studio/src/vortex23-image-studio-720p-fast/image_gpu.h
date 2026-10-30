#ifndef IMAGE_GPU_H
#define IMAGE_GPU_H
#include "image_contract.h"
typedef struct {uint32_t jobs_before,jobs_after,cycles,errors,pending_rd,pending_wr;} image_gpu_stats;
int image_gpu_init(void);
int image_gpu_run(enum image_op op,const image_job *job,image_gpu_stats *stats);
uint64_t image_ticks(void);
uint64_t image_timer_hz(void);
void host_service_poll(void);
#endif
