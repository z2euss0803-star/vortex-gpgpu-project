/* Test-only architectural four-round semantics, never part of target builds. */
#include <stdint.h>
typedef struct {uint32_t v[4];} uint32x4_t;
static uint32x4_t vld1q_u32(const uint32_t *p){uint32x4_t r;for(unsigned i=0;i<4;i++)r.v[i]=p[i];return r;}
static void vst1q_u32(uint32_t *p,uint32x4_t v){for(unsigned i=0;i<4;i++)p[i]=v.v[i];}
static uint32x4_t vaddq_u32(uint32x4_t a,uint32x4_t b){for(unsigned i=0;i<4;i++)a.v[i]+=b.v[i];return a;}
static uint32_t rr(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
static void rounds(uint32x4_t *abcd,uint32x4_t *efgh,uint32x4_t wk){
 uint32_t a=abcd->v[0],b=abcd->v[1],c=abcd->v[2],d=abcd->v[3];
 uint32_t e=efgh->v[0],f=efgh->v[1],g=efgh->v[2],h=efgh->v[3];
 for(unsigned i=0;i<4;i++){
  uint32_t t=h+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+wk.v[i];
  uint32_t u=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));
  h=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;
 }
 *abcd=(uint32x4_t){{a,b,c,d}};*efgh=(uint32x4_t){{e,f,g,h}};
}
static uint32x4_t vsha256hq_u32(uint32x4_t a,uint32x4_t e,uint32x4_t w){rounds(&a,&e,w);return a;}
static uint32x4_t vsha256h2q_u32(uint32x4_t e,uint32x4_t a,uint32x4_t w){rounds(&a,&e,w);return e;}
