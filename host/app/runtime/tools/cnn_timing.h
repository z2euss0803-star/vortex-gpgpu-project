#ifndef CNN_TIMING_H
#define CNN_TIMING_H
#include <cstdint>
using CnnNowFn=int (*)(void *,uint64_t *);
struct CnnInterval {uint64_t start_ns=0,end_ns=0,elapsed_ns=0;int result=-61;bool started=false,valid=false;};
int cnn_monotonic_now(void *,uint64_t *);
int cnn_clock_resolution(uint64_t *);
int cnn_elapsed_ns(uint64_t,uint64_t,uint64_t *);
int cnn_interval_begin(CnnInterval *,CnnNowFn,void *);
int cnn_interval_end(CnnInterval *,CnnNowFn,void *);
#endif
