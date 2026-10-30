#include "cnn_timing.h"
#include <cerrno>
#include <ctime>
static int convert(const timespec &t,uint64_t *n){
 if(!n||t.tv_sec<0||t.tv_nsec<0||t.tv_nsec>=1000000000)return -EINVAL;
 if(static_cast<uint64_t>(t.tv_sec)>(UINT64_MAX-static_cast<uint64_t>(t.tv_nsec))/1000000000)return -EOVERFLOW;
 *n=static_cast<uint64_t>(t.tv_sec)*1000000000+static_cast<uint64_t>(t.tv_nsec);return 0;
}
int cnn_elapsed_ns(uint64_t a,uint64_t b,uint64_t *v){if(!v)return -EINVAL;if(b<a)return -ERANGE;*v=b-a;return 0;}
int cnn_monotonic_now(void *,uint64_t *n){timespec t{};if(!n)return -EINVAL;if(clock_gettime(CLOCK_MONOTONIC,&t))return -errno;return convert(t,n);}
int cnn_clock_resolution(uint64_t *n){timespec t{};if(!n)return -EINVAL;if(clock_getres(CLOCK_MONOTONIC,&t))return -errno;return convert(t,n);}
int cnn_interval_begin(CnnInterval *t,CnnNowFn now,void *ctx){
 if(!t||!now)return -EINVAL;
 *t=CnnInterval{};uint64_t value=0;t->result=now(ctx,&value);
 if(!t->result){t->start_ns=value;t->started=true;}return t->result;
}
int cnn_interval_end(CnnInterval *t,CnnNowFn now,void *ctx){
 if(!t||!now)return -EINVAL;
 if(!t->started)return -EINVAL;
 uint64_t value=0;t->valid=false;t->result=now(ctx,&value);if(t->result)return t->result;
 t->end_ns=value;t->result=cnn_elapsed_ns(t->start_ns,t->end_ns,&t->elapsed_ns);t->valid=!t->result;return t->result;
}
