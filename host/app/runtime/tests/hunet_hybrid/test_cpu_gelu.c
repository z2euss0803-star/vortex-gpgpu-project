#include <vxl/hunet_cpu_gelu.h>
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int creates,joins,fail_at;
int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
int __real_pthread_join(pthread_t,void **);
int __wrap_pthread_create(pthread_t *t,const pthread_attr_t *a,void *(*f)(void *),void *p){
 creates++;if(fail_at&&creates==fail_at)return EAGAIN;return __real_pthread_create(t,a,f,p);
}
int __wrap_pthread_join(pthread_t t,void **p){joins++;return __real_pthread_join(t,p);}
static float reference(float x){return (x*.5f)*(1.0f+erff(x/1.4142135623730951f));}
static void boundary(size_t n){
 float *x=malloc(n*4),*y=malloc(n*4+64);struct hunet_cpu_timing t;assert(x&&y);
 for(size_t i=0;i<n;i++)x[i]=((int)(i%2001)-1000)/79.f;
 if(n>3){x[0]=0;x[1]=-0.f;x[2]=1e30f;x[3]=-1e30f;}
 memset(y,0xa5,n*4+64);creates=joins=0;
 assert(!hunet_cpu_gelu4(x,y,n,&t));assert(creates==4&&joins==4&&t.threads==4);
 assert(t.end_ns>=t.begin_ns&&t.duration_ns==t.end_ns-t.begin_ns);
 for(size_t i=0;i<n;i++){float r=reference(x[i]);assert(!memcmp(&r,&y[i],4));}
 for(size_t i=n*4;i<n*4+64;i++)assert(((unsigned char*)y)[i]==0xa5);
 free(x);free(y);
}
static void captures(const char *dir){
 unsigned stages[]={8,17,28,37,49,55,61,67,73,79,87,93,98};
 size_t counts[]={393216,393216,196608,196608,98304,98304,98304,98304,98304,98304,49152,49152,1280};
 for(unsigned s=0;s<13;s++){
  size_t n=counts[s];float *x=malloc(n*4),*y=malloc(n*4),*g=malloc(n*4);assert(x&&y&&g);char p[2048];FILE *f;
  snprintf(p,sizeof(p),"%s/data/%03u-input.f32",dir,stages[s]);f=fopen(p,"rb");assert(f&&fread(x,4,n,f)==n&&fgetc(f)==EOF&&!fclose(f));
  snprintf(p,sizeof(p),"%s/data/%03u-gpu_reference.f32",dir,stages[s]);f=fopen(p,"rb");assert(f&&fread(g,4,n,f)==n&&fgetc(f)==EOF&&!fclose(f));
  struct hunet_cpu_timing t;assert(!hunet_cpu_gelu4(x,y,n,&t));size_t diffs=0;double max=0;
  for(size_t i=0;i<n;i++){float r=reference(x[i]);assert(!memcmp(&r,&y[i],4));if(memcmp(&g[i],&y[i],4))diffs++;double d=fabs((double)g[i]-y[i]);if(d>max)max=d;}
  printf("{\"scope\":\"A_HOST_CAPTURE_REPLAY_NOT_A53_OR_HYBRID\",\"stage\":%u,\"elements\":%zu,\"bit_diff_vs_gpu\":%zu,\"max_abs_vs_gpu\":%.17g,\"host_cpu_ns\":%llu}\n",stages[s],n,diffs,max,(unsigned long long)t.duration_ns);
  free(x);free(y);free(g);
 }
}
int main(int argc,char **argv){
 size_t sizes[]={1,3,4,1280,49152,393216};for(unsigned i=0;i<6;i++)boundary(sizes[i]);
 float x[8]={1,2,3,4,5,6,7,8},y[8];struct hunet_cpu_timing t;
 memset(y,0xa5,sizeof(y));memset(&t,0xa5,sizeof(t));
 assert(hunet_cpu_gelu4(x,y,0,&t)==-EINVAL);assert(hunet_cpu_gelu4(x,y,393217,&t)==-EINVAL);
 assert(hunet_cpu_gelu4(x,x,4,&t)==-EINVAL);assert(hunet_cpu_gelu4(x,x+1,4,&t)==-EINVAL);
 assert(hunet_cpu_gelu4(NULL,y,4,&t)==-EINVAL);assert(hunet_cpu_gelu4(x,y,4,NULL)==-EINVAL);
 x[2]=NAN;assert(hunet_cpu_gelu4(x,y,4,&t)==-EDOM);x[2]=INFINITY;assert(hunet_cpu_gelu4(x,y,4,&t)==-EDOM);x[2]=3;
 assert(!fesetround(FE_DOWNWARD));assert(hunet_cpu_gelu4(x,y,4,&t)==-EINVAL);assert(!fesetround(FE_TONEAREST));
 creates=joins=0;fail_at=2;assert(hunet_cpu_gelu4(x,y,8,&t)==-EAGAIN);assert(creates==2&&joins==1);
 for(size_t i=0;i<sizeof(y);i++)assert(((unsigned char*)y)[i]==0xa5);
 assert(t.duration_ns==0&&t.threads==0);fail_at=0;
 if(argc==2)captures(argv[1]);
 puts("HOST_CPU_GELU_BOUNDARIES_THREAD_FAILURE_PASS_NOT_BOARD");return 0;
}
