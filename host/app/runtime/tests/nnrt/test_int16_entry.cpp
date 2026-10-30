#include "device/vxnn/ops.h"
#include <cstdio>
#include <cstdlib>
static void need(bool b,const char*s){if(!b){std::fprintf(stderr,"U11_ENTRY_ASSERT %s\n",s);std::exit(62);}}
static vxnn_i16_job_v1 valid(){vxnn_i16_job_v1 j{};j.version=1;j.bytes=128;j.op=VXNN_MATMUL_I16;j.m=8;j.n=8;j.k=8;j.a_gpu=0x82000000;j.b_gpu=0x82000100;j.out_gpu=0x82000200;j.a_bytes=128;j.b_bytes=128;j.out_bytes=512;j.generation_low=1;j.sequence_low=1;j.cookie_low=1;j.binding[0]=0x42;return j;}
int main(){
 auto j=valid();need(!vxnn_i16_validate_job(&j),"valid native INT16 controlled argument must be accepted");
 for(unsigned which=0;which<16;which++){
  j=valid();switch(which){case 0:j.version=2;break;case 1:j.bytes=127;break;case 2:j.op=9;break;case 3:j.m=7;break;case 4:j.n=72;break;case 5:j.k=65;break;case 6:j.a_gpu=0x10000000;break;case 7:j.out_gpu=0xfff00000;break;case 8:j.a_gpu=0;break;case 9:j.a_bytes--;break;case 10:j.out_bytes--;break;case 11:j.out_gpu=j.a_gpu;break;case 12:j.generation_low=0;break;case 13:j.sequence_low=0;break;case 14:j.cookie_low=0;break;case 15:j.reserved[9]=1;break;}
  need(vxnn_i16_validate_job(&j)<0,"malformed shape/address/identity rejected before native execution");
 }
 j=valid();j.binding[0]=0;need(vxnn_i16_validate_job(&j)<0,"binding required");need(vxnn_i16_validate_job(nullptr)<0,"null payload rejected");
 j=valid();j.op=VXNN_BIAS_I48;j.k=0;j.a_bytes=512;j.b_bytes=32;j.b_gpu=0x82000400;j.out_gpu=0x82000800;need(!vxnn_i16_validate_job(&j),"i48/i32 bias layout");j.b_bytes=64;need(vxnn_i16_validate_job(&j)<0,"bias is signed i32 not implicit i64");
 j=valid();j.op=VXNN_RELU_I48;j.k=0;j.a_bytes=512;j.b_gpu=j.a_gpu;j.b_bytes=j.a_bytes;j.out_gpu=0x82000800;need(!vxnn_i16_validate_job(&j),"relu canonical A alias fits fixed three-ref sealed schema");j.b_gpu=0x82000400;need(vxnn_i16_validate_job(&j)<0,"canonical alias cannot name unrelated resource");j.b_gpu=0;j.b_bytes=0;need(vxnn_i16_validate_job(&j)<0,"zero optional pointer cannot bypass fixed schema");
 puts("U11_PRIVATE128_ENTRY_ADDRESS_SHAPE_IDENTITY_HOST_VALIDATION_NOT_RV32_OR_GPU gpu_raw_cycles=null");
}
