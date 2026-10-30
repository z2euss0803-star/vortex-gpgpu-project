/* Actual shared-PC RV32 instruction model; no RTL/cache/timing claim. */
#ifndef RV32_IMAGE_MODEL_H
#define RV32_IMAGE_MODEL_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "image_contract.h"
static uint32_t *im_memory;
static uint8_t im_written[IMAGE_WIDTH*IMAGE_HEIGHT];
static uint32_t im_error,im_stores;
static uint64_t im_instructions;
static uint32_t *im_pointer(uint32_t a){
 if(a<IMAGE_SLOT_BASE || a>=IMAGE_SLOT_BASE+IMAGE_SLOT_COUNT*IMAGE_SLOT_SIZE || (a&3u))return NULL;
 uint32_t off=a-IMAGE_SLOT_BASE,slot=off/IMAGE_SLOT_SIZE,local=off%IMAGE_SLOT_SIZE;
 uint32_t bytes=slot==10u?65536u:IMAGE_BYTES;
 if(local<64u || local>=64u+bytes)return NULL;
 return im_memory+off/4u;
}
static int32_t im_sx(uint32_t x,unsigned bits){uint32_t s=1u<<(bits-1);return (int32_t)((x&(s-1))-(x&s));}
static uint32_t im_load(const image_job *j,uint32_t a){
 if(a>=0x10010000u&&a<0x10010040u&&!(a&3u)){uint32_t words[16];memcpy(words,j,64);return words[(a-0x10010000u)/4u];}
 uint32_t n=j->width*j->height*4u;int allowed=0;
 const uint32_t src[3]={j->src0,j->src1,j->src2};
 for(unsigned i=0;i<3;i++)if(src[i]&&a>=src[i]&&a<src[i]+n)allowed=1;
 if(a>=IMAGE_TABLE&&a<IMAGE_TABLE+65536u)allowed=1;
 uint32_t *p=im_pointer(a);if(!p||!allowed){im_error=1;return 0;}return *p;
}
static void im_store(const image_job *j,uint32_t a,uint32_t v,uint32_t count){
 uint32_t *p=im_pointer(a);
 if(!p||a<j->dst||a>=j->dst+count*4u){im_error=2;return;}
 uint32_t index=(a-j->dst)/4u;if(im_written[index]){im_error=3;return;}
 im_written[index]=1;im_stores++;*p=v;
}
static int im_run(const uint32_t *code,uint32_t words,const image_job *j,uint32_t count){
 im_error=im_stores=0;memset(im_written,0,sizeof(im_written));
 for(unsigned warp=0;warp<16u;warp++){
  uint32_t r[4][32]={{0}},pc=24u;unsigned retired=0,fenced=0;
  for(uint32_t step=0;step<100000000u;step++){
   if((pc&3u)||pc/4u>=words){im_error=4;break;}
   uint32_t word=code[pc/4u],op=word&127u,rd=(word>>7)&31u,f3=(word>>12)&7u,rs1=(word>>15)&31u,rs2=(word>>20)&31u,f7=word>>25,next=pc+4u;
   im_instructions+=4u;
   if(op==0x0bu&&f3==0u){if(!fenced)im_error=5;retired=1;break;}
   if(op==0x0fu){fenced=1;pc=next;continue;}
   if(op==0x63u){
    uint32_t imm=((word>>31)<<12)|(((word>>7)&1u)<<11)|(((word>>25)&63u)<<5)|(((word>>8)&15u)<<1);unsigned taken[4];
    for(unsigned lane=0;lane<4u;lane++){
     uint32_t a=r[lane][rs1],b=r[lane][rs2];
     if(f3==0)taken[lane]=a==b;else if(f3==7)taken[lane]=a>=b;else if(f3==4)taken[lane]=(int32_t)a<(int32_t)b;else{im_error=6;break;}
    }
    if(im_error)break;
    for(unsigned lane=0;lane<3u;lane++)if(taken[lane]!=taken[3])im_error=7;
    if(im_error)break;
    if(taken[3])next=pc+(uint32_t)im_sx(imm,13);pc=next;continue;
   }
   if(op==0x6fu){uint32_t imm=((word>>31)<<20)|(((word>>12)&255u)<<12)|(((word>>20)&1u)<<11)|(((word>>21)&1023u)<<1);for(unsigned lane=0;lane<4;lane++)if(rd)r[lane][rd]=next;next=pc+(uint32_t)im_sx(imm,21);pc=next;continue;}
   for(unsigned lane=0;lane<4;lane++){
    uint32_t *q=r[lane],a=q[rs1],b=q[rs2],value=0;int write=1;
    if(op==0x73u&&f3==2u){uint32_t csr=word>>20;if(csr==0x340u)value=0x10010000u;else if(csr==0xf14u)value=4u*warp+lane;else im_error=8;}
    else if(op==0x37u)value=word&0xfffff000u;
    else if(op==0x03u&&f3==2u)value=im_load(j,a+(uint32_t)im_sx(word>>20,12));
    else if(op==0x23u&&f3==2u){uint32_t imm=((word>>25)<<5)|((word>>7)&31u);im_store(j,a+(uint32_t)im_sx(imm,12),b,count);write=0;}
    else if(op==0x13u){uint32_t sh=(word>>20)&31u;if(f3==0)value=a+(uint32_t)im_sx(word>>20,12);else if(f3==7)value=a&(uint32_t)im_sx(word>>20,12);else if(f3==1)value=a<<sh;else if(f3==5)value=(word&(1u<<30))?(uint32_t)((int32_t)a>>sh):a>>sh;else im_error=9;}
    else if(op==0x33u){
     if(f7==1&&f3==0)value=(uint32_t)((uint64_t)a*b);else if(f7==1&&f3==5)value=b?a/b:UINT32_MAX;
     else if(f7==32&&f3==0)value=a-b;else if(!f7&&f3==0)value=a+b;else if(!f7&&f3==3)value=a<b;
     else if(!f7&&f3==4)value=a^b;else if(!f7&&f3==6)value=a|b;else if(!f7&&f3==7)value=a&b;else im_error=10;
    }else im_error=11;
    if(im_error)break;if(write&&rd)q[rd]=value;q[0]=0;
   }
   if(im_error)break;pc=next;
  }
  if(im_error)return (int)im_error;if(!retired)return 12;
 }
 return im_stores==count?0:13;
}
#endif
