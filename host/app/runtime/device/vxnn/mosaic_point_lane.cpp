#include "mosaic_point_lane.h"
#include "device/mosaic_s8/graph.h"
#include "device/mosaic_s8/norm_compat.h"
#include <errno.h>
#include <math.h>
#if !defined(__riscv)
#include <fenv.h>
#endif
namespace {
bool round_to_nearest(){
#if defined(__riscv)
 uint32_t mode;__asm__ volatile("csrr %0, frm":"=r"(mode));return mode==0;
#else
 return fegetround()==FE_TONEAREST;
#endif
}
struct Range {const void *p;uint32_t bytes;bool write;};
bool add(Range *ranges,unsigned &count,const void *p,uint32_t bytes,uint32_t alignment,bool write,bool allow_input_alias=false){
 uintptr_t address=reinterpret_cast<uintptr_t>(p);
 if(!p||!bytes||address%alignment||bytes>UINTPTR_MAX-address)return false;
 for(unsigned i=0;i<count;++i)if((write||ranges[i].write)&&address<reinterpret_cast<uintptr_t>(ranges[i].p)+ranges[i].bytes&&reinterpret_cast<uintptr_t>(ranges[i].p)<address+bytes){
  if(!(allow_input_alias&&i==0&&p==ranges[0].p&&bytes==ranges[0].bytes))return false;
 }
 if(count>=8)return false;
 ranges[count++]={p,bytes,write};return true;
}
int admission(const mosaic_point_lane_args *a){
 if(!a||a->lane>3||(a->opcode!=1&&a->opcode!=4&&a->opcode!=5&&a->opcode!=6&&a->opcode!=7&&a->opcode!=8)||!round_to_nearest())return -EINVAL;
 if(!a->full_spatial||a->full_spatial>1280u*960u||!a->full_elements||a->full_elements%a->full_spatial||a->full_elements/a->full_spatial>128||!a->count||a->count>256)return -EINVAL;
 if(a->first>=a->full_elements||a->count>a->full_elements-a->first||(a->first%256)+a->count>256)return -ERANGE;
 bool quant=a->opcode==1||a->opcode>=7,norm=a->opcode==4||a->opcode>=7;
 if(a->save_input&&!quant)return -EINVAL;
 if(quant&&(!isfinite(a->scale)||a->scale<=0))return -EINVAL;
 const uint32_t channels=a->full_elements/a->full_spatial,bytes=a->full_elements*4;
 Range ranges[8];unsigned count=0;
 if(!add(ranges,count,a->input,bytes,4,false))return -EINVAL;
 if(norm&&(!add(ranges,count,a->stats,channels*12,4,false)||!add(ranges,count,a->gamma,channels*4,4,false)||!add(ranges,count,a->beta,channels*4,4,false)))return -EINVAL;
 if((a->opcode==6||a->opcode==8)&&!add(ranges,count,a->skip,bytes,4,false))return -EINVAL;
 if(quant&&!add(ranges,count,a->quant,a->full_elements*2,2,true))return -EINVAL;
 if((!quant||(a->opcode>=7&&a->save_input))&&!add(ranges,count,a->output,bytes,4,true,!quant))return -EINVAL;
 if(a->opcode==1&&a->save_input&&!add(ranges,count,a->saved_input,bytes,4,true))return -EINVAL;
 return 0;
}
}
int mosaic_point_lane_prepare(const mosaic_point_lane_args *a,mosaic_point_lane_prepared *s){
 if(!s)return -EINVAL;
 s->ready=false;
 int rc=admission(a);if(rc)return rc;
 const uint32_t offset=a->count*a->lane/4,end=a->count*(a->lane+1)/4;
 s->first=a->first+offset;s->count=end-offset;s->opcode=a->opcode;s->save_input=a->save_input;
 s->output=a->output;s->saved_input=a->saved_input;s->quant=a->quant;
 if(!s->count){s->ready=true;return 0;}
 if(a->opcode==4||a->opcode>=7){
  uint32_t done=0;
  while(done<s->count){
   uint32_t pos=s->first+done,channel=pos/a->full_spatial,part=a->full_spatial-pos%a->full_spatial;
   if(part>s->count-done)part=s->count-done;
   // Full channel size selects V2/V3; the local part only limits this call.
   rc=mosaic_norm_apply_compat_v3(a->input+pos,part,a->stats+channel*3,a->gamma[channel],a->beta[channel],a->full_spatial,s->values+done);
   if(rc)return rc;
   done+=part;
  }
 }else for(uint32_t i=0;i<s->count;++i)s->values[i]=a->input[s->first+i];
 if(a->opcode==5||a->opcode==6||a->opcode>=7){
  for(uint32_t i=0;i<s->count;++i){
   if(!isfinite(s->values[i]))return -EDOM;
   if((a->opcode==6||a->opcode==8)&&!isfinite(a->skip[s->first+i]))return -EDOM;
   volatile float value=a->opcode==5||a->opcode==7?(s->values[i]>0?s->values[i]:0.f):s->values[i]+a->skip[s->first+i];
   if(!isfinite(value))return -ERANGE;
   s->values[i]=value;
  }
 }
 if(a->opcode==1||a->opcode>=7){
  volatile float inverse=1.f/a->scale;
  rc=mosaic_quantize_input(s->values,s->count,inverse,s->quantized);if(rc)return rc;
 }
 s->ready=true;return 0;
}
void mosaic_point_lane_commit(const mosaic_point_lane_prepared *s,int agreed_rc,mosaic_point_lane_commit_phase phase){
 if(!s||agreed_rc||!s->ready)return;
 const bool quant=s->opcode==1||s->opcode>=7;
 if(phase==MOSAIC_POINT_COMMIT_QUANT){
  if(quant)for(uint32_t i=0;i<s->count;++i)s->quant[s->first+i]=s->quantized[i];
 }else if(phase==MOSAIC_POINT_COMMIT_FLOAT){
  if(quant){
   // Caller synchronizes all lanes after QUANT before entering FLOAT.
   if(s->save_input){
    float *dest=s->opcode==1?s->saved_input:s->output;
    for(uint32_t i=0;i<s->count;++i)dest[s->first+i]=s->values[i];
   }
  }else for(uint32_t i=0;i<s->count;++i)s->output[s->first+i]=s->values[i];
 }
}
