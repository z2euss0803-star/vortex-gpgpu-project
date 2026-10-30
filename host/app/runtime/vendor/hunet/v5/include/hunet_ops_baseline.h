#pragma once
#include "hunet_job.h"
#include "hunet_math.h"

/* Called once per independent scalar task. Matrix arithmetic lives in WMMA code. */
static inline void hunet_scalar_baseline(const HunetJobV1 *j,uint32_t t) {
    const float *x=(const float*)(uintptr_t)j->x;
    const float *w=(const float*)(uintptr_t)j->w;
    float *y=(float*)(uintptr_t)j->y;
    uint32_t hw=j->ih*j->iw, ohw=j->oh*j->ow;
    const HunetMatrix *m=(const HunetMatrix*)(uintptr_t)j->aux;
    switch(j->op) {
    case HUNET_PACK: {
        uint32_t kk=t/m->NP,nn=t%m->NP;float value=0;
        if(kk<m->K && nn<m->count) {
            uint32_t pos=m->offset+nn;
            if(j->flags==2) {
                int ix=(int)pos+(int)kk-(int)j->pad;
                if(ix>=0 && ix<(int)j->iw)value=x[ix];
            } else {
                uint32_t ic=kk/(j->k*j->k),ky=(kk/j->k)%j->k,kx=kk%j->k;
                int iy=(int)((pos/j->ow)*j->stride+ky)-(int)j->pad;
                int ix=(int)((pos%j->ow)*j->stride+kx)-(int)j->pad;
                if(iy>=0 && ix>=0 && iy<(int)j->ih && ix<(int)j->iw)value=x[ic*hw+(uint32_t)iy*j->iw+(uint32_t)ix];
            }
        }
        ((uint16_t*)y)[t]=hunet_half(value);break;
    }
    case HUNET_UNPACK: {
        uint32_t row=t/m->count,col=t%m->count;
        float value=x[row*m->NP+col];if(j->bias)value+=((const float*)(uintptr_t)j->bias)[row];
        y[row*m->N+m->offset+col]=value;break;
    }
    case HUNET_GN_STATS: {
        float sum=0,carry=0;uint32_t size=j->ci*hw;
        for(uint32_t i=0;i<size;++i)hunet_kahan_add(x[i],&sum,&carry);
        float mean=sum/(float)size;sum=0;carry=0;
        for(uint32_t i=0;i<size;++i){float v=x[i]-mean;hunet_kahan_add(v*v,&sum,&carry);}
        y[0]=mean;y[1]=1.0f/sqrtf(sum/(float)size+hunet_float(j->epsilon_bits));break;
    }
    case HUNET_GN_APPLY: {
        const float *stats=(const float*)(uintptr_t)j->scratch;
        const float *gamma=(const float*)(uintptr_t)j->gamma,*beta=(const float*)(uintptr_t)j->beta;
        y[t]=((x[t]-stats[0])*stats[1])*gamma[t/hw]+beta[t/hw];break;
    }
    case HUNET_GELU:y[t]=(x[t]*.5f)*(1.0f+erff(x[t]/1.4142135623730951f));break;
    case HUNET_SIGMOID:y[t]=1.0f/(1.0f+expf(-x[t]));break;
    case HUNET_ADD:y[t]=x[t]+w[t];break;
    case HUNET_MUL_CHANNEL:y[t]=x[t]*w[t/ohw];break;
    case HUNET_COPY:y[t]=x[t];break;
    case HUNET_SLICE:y[t]=x[j->groups*hw+t];break;
    case HUNET_CONCAT:y[t]=(t<j->ci*hw)?x[t]:w[t-j->ci*hw];break;
    case HUNET_GAP: {
        float sum=0,carry=0;for(uint32_t i=0;i<hw;++i)hunet_kahan_add(x[t*hw+i],&sum,&carry);
        y[t]=sum/(float)hw;break;
    }
    case HUNET_SHIFT: {
        uint32_t channel=t/hw,row=(t%hw)/j->iw,col=t%j->iw,g=j->ci/j->n_div;
        int iy=(int)row,ix=(int)col;
        if(channel<g)++ix;else if(channel<2*g)--ix;else if(channel<3*g)++iy;else if(channel<4*g)--iy;
        y[t]=(iy>=0 && ix>=0 && iy<(int)j->ih && ix<(int)j->iw)?x[channel*hw+(uint32_t)iy*j->iw+(uint32_t)ix]:0.0f;
        break;
    }
    }
}
