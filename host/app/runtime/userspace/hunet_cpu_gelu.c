#define _POSIX_C_SOURCE 200809L
#include <vxl/hunet_cpu_gelu.h>
#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
struct work {const float *x;float *y;size_t begin,end;};
static void *compute(void *ptr){
 struct work *w=ptr;
 for(size_t i=w->begin;i<w->end;i++){
  float x=w->x[i];w->y[i]=(x*.5f)*(1.0f+erff(x/1.4142135623730951f));
 }
 return NULL;
}
static int now(uint64_t *out){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -errno;*out=(uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec;return 0;}
int hunet_cpu_gelu4(const float *x,float *y,size_t n,struct hunet_cpu_timing *out){
 struct {struct work work[4];float values[];} *staging;
 struct hunet_cpu_timing t={0};pthread_t threads[4];unsigned created=0;int rc=0,unknown_worker=0;
 if(out)memset(out,0,sizeof(*out));
 if(!x||!y||!out||!n||n>393216||fegetround()!=FE_TONEAREST)return -EINVAL;
 size_t bytes=n*sizeof(float);uintptr_t a=(uintptr_t)x,b=(uintptr_t)y;
 if(a>UINTPTR_MAX-bytes||b>UINTPTR_MAX-bytes||(a<b+bytes&&b<a+bytes))return -EINVAL;
 for(size_t i=0;i<n;i++)if(!isfinite(x[i]))return -EDOM;
 staging=malloc(sizeof(*staging)+bytes);if(!staging)return -ENOMEM;
 rc=now(&t.begin_ns);if(rc){free(staging);return rc;}
 for(unsigned i=0;i<4;i++){
  staging->work[i]=(struct work){x,staging->values,n*i/4,n*(i+1)/4};
  int e=pthread_create(&threads[i],NULL,compute,&staging->work[i]);if(e){rc=-e;break;}created++;
 }
 for(unsigned i=0;i<created;i++){
  int e=pthread_join(threads[i],NULL);if(e){rc=-e;unknown_worker=1;}
 }
 /* A join failure does not prove worker quiescence. Retain its heap context;
    the caller must abort the candidate rather than publish or reuse it. */
 if(unknown_worker)return rc;
 if(!rc)rc=now(&t.end_ns);
 if(!rc){
  if(t.end_ns<t.begin_ns)rc=-EIO;
  else for(size_t i=0;i<n;i++)if(!isfinite(staging->values[i])){rc=-EDOM;break;}
 }
 if(!rc){t.duration_ns=t.end_ns-t.begin_ns;t.threads=4;memcpy(y,staging->values,bytes);*out=t;}
 free(staging);return rc;
}
