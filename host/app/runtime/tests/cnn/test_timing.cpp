#include "../../tools/cnn_timing.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
struct Fake {uint64_t ns;int error;};
static int fake(void *c,uint64_t *n){auto *f=static_cast<Fake*>(c);if(f->error)return f->error;*n=f->ns;return 0;}
int main(){
 uint64_t n=42,a,b;assert(!cnn_elapsed_ns(100,250,&n)&&n==150);
 assert(cnn_elapsed_ns(250,100,&n)==-ERANGE&&n==150);
 assert(!cnn_elapsed_ns(1,UINT64_C(0x200000001),&n)&&n==UINT64_C(0x200000000));
 assert(cnn_elapsed_ns(0,1,nullptr)==-EINVAL);
 Fake f{100,0};CnnInterval t;
 assert(!cnn_interval_begin(&t,fake,&f));f.ns=250;assert(!cnn_interval_end(&t,fake,&f)&&t.valid&&t.elapsed_ns==150);
 f.error=-EIO;assert(cnn_interval_begin(&t,fake,&f)==-EIO&&!t.valid&&!t.started);
 f.error=0;f.ns=250;assert(!cnn_interval_begin(&t,fake,&f));f.ns=100;assert(cnn_interval_end(&t,fake,&f)==-ERANGE&&!t.valid);
 f.ns=100;assert(!cnn_interval_begin(&t,fake,&f));f.error=-EIO;assert(cnn_interval_end(&t,fake,&f)==-EIO&&!t.valid);
 assert(!cnn_monotonic_now(nullptr,&a));assert(!cnn_monotonic_now(nullptr,&b)&&b>=a);assert(!cnn_clock_resolution(&n)&&n>0);
 puts("MONOTONIC_UINT64_FAILURE_PROPAGATION_PASS_NOT_BOARD_TIMING");
}
