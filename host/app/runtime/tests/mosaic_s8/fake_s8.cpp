#include "fake_s8.h"
#include <errno.h>
#include <string.h>
static void words(mosaic_words4 *o,uint32_t a,uint32_t b,uint32_t c,uint32_t d){*o={{a,b,c,d}};}
static int begin(void *p,uint32_t k,mosaic_words4 *o){
 auto &f=*static_cast<fake_s8*>(p);++f.begins;if(f.fail_begin)return -EIO;
 if(f.active)return -EBUSY;
 f.k=k;f.accepted=0;f.bitmap=0;f.current_polls=0;f.active=true;
 memset(f.sums,0,sizeof(f.sums));words(o,f.bad_begin?1:0,0,k,1);return 0;
}
static int step(void *p,const mosaic_words4 *a,const mosaic_words4 *b){
 auto &f=*static_cast<fake_s8*>(p);++f.steps;if(int(f.accepted)==f.fail_step)return -EIO;
 int32_t aa[8],bb[8];
 for(unsigned x=0;x<8;++x){uint32_t av=(a->word[x/2]>>(16*(x%2)))&65535,bv=(b->word[x/2]>>(16*(x%2)))&65535;aa[x]=av<32768?int32_t(av):int32_t(av)-65536;bb[x]=bv<32768?int32_t(bv):int32_t(bv)-65536;}
 for(unsigned row=0;row<8;++row)for(unsigned col=0;col<8;++col)f.sums[row*8+col]+=int64_t(aa[row])*bb[col];
 ++f.accepted;return 0;
}
static int read(void *p,uint32_t control,mosaic_words4 *o){
 auto &f=*static_cast<fake_s8*>(p);++f.reads;
 if(control==31){
  if(!f.active){words(o,f.bad_idle?1:0,0,0,0);return 0;}
  ++f.polls;++f.current_polls;if(f.bad_status){words(o,99,f.accepted,f.k,1);return 0;}
  if(f.bitmap==0xffffff){words(o,0,f.accepted,f.k,7);return 0;}
  words(o,(f.never_ready||f.current_polls<=f.ready_after)?4:0,f.accepted,f.k,(f.never_ready||f.current_polls<=f.ready_after)?1:3);return 0;
 }
 if(control>=24||int(control)==f.fail_page)return -EIO;
 f.bitmap|=1u<<control;
 // Independent bitwise signed48 packing; no production unpack routine used.
 uint32_t data[96]={};
 for(unsigned n=0;n<64;++n){uint64_t v=uint64_t(f.sums[n])&0xffffffffffffULL;for(unsigned b=0;b<48;++b)if((v>>b)&1)data[(n*48+b)/32]|=1u<<((n*48+b)%32);}
 for(unsigned lane=0;lane<4;++lane){o->word[lane]=data[control*4+lane];}
 return 0;
}
static int release(void *p,mosaic_words4 *o){auto &f=*static_cast<fake_s8*>(p);++f.releases;if(f.fail_release)return -EIO;words(o,0,f.accepted,f.k,7);if(f.bitmap!=0xffffff)return -EPROTO;f.active=false;return 0;}
static void fence(void *p){++static_cast<fake_s8*>(p)->fences;}
static uint64_t now(void *p){return ++static_cast<fake_s8*>(p)->ticks;}
mosaic_s8_ops fake_s8::ops(){return {this,begin,step,read,release,fence,now,0,0,4,64,0};}
