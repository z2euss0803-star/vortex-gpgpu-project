#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

static inline float hunet_float(uint32_t bits) { float x; memcpy(&x,&bits,4); return x; }
static inline uint32_t hunet_bits(float x) { uint32_t bits; memcpy(&bits,&x,4); return bits; }
/* Explicit IEEE RNE conversion; independent of compiler half extensions. */
static inline uint16_t hunet_half(float x) {
    uint32_t u=hunet_bits(x), sign=(u>>16)&0x8000u, frac=u&0x7fffffu;
    int exp=(int)((u>>23)&255)-127+15;
    if (((u>>23)&255)==255) return (uint16_t)(sign|0x7c00u|(frac?0x200u:0));
    if (exp>=31) return (uint16_t)(sign|0x7c00u);
    if (exp<=0) {
        if (exp < -10) return (uint16_t)sign;
        uint32_t mant=frac|0x800000u, shift=(uint32_t)(14-exp), q=mant>>shift;
        uint32_t rem=mant&((1u<<shift)-1), half=1u<<(shift-1);
        q += rem>half || (rem==half && (q&1));
        return (uint16_t)(sign|q);
    }
    uint32_t q=((uint32_t)exp<<10)|(frac>>13), rem=frac&8191;
    q += rem>4096 || (rem==4096 && (q&1));
    return (uint16_t)(sign|q);
}
static inline float hunet_unhalf(uint16_t h) {
    uint32_t s=((uint32_t)h&0x8000u)<<16,e=(h>>10)&31,f=h&1023;
    if(e==31)return hunet_float(s|0x7f800000u|(f?0x400000u|(f<<12):0));
    if(e==0) {
        if(f==0)return hunet_float(s);
        int shift=0;while(!(f&512)){f<<=1;++shift;}
        return hunet_float(s|((uint32_t)(112-shift)<<23)|((f&511)<<14));
    }
    return hunet_float(s|((e+112)<<23)|(f<<13));
}
static inline void hunet_kahan_add(float v,float *sum,float *carry) {
    float corrected=v-*carry,next=*sum+corrected;
    *carry=(next-*sum)-corrected;*sum=next;
}
