#pragma once
#include <stddef.h>
#include <stdint.h>
#include "device/mosaic_s8/tile.h"
// New user-owned program argument semantics; not a kernel ioctl structure.
// Matrices are row major. i48 results occupy signed, sign-extended i64 slots.
// Fixed3-reference sealed schema: A READ, B READ, OUT WRITE. For ReLU, B is
// the canonical identical A reference; it is not another numerical operand.
enum vxnn_i16_op : uint32_t {VXNN_MATMUL_I16=0x10001,VXNN_BIAS_I48=0x10002,VXNN_RELU_I48=0x10003};
struct vxnn_i16_job_v1 {
 uint32_t version,bytes,op,m,n,k;
 uint32_t a_gpu,b_gpu,out_gpu,a_bytes,b_bytes,out_bytes;
 uint32_t generation_low,generation_high,sequence_low,sequence_high,cookie_low,cookie_high;
 uint8_t binding[16];uint32_t reserved[10];
};
static_assert(sizeof(vxnn_i16_job_v1)==128,"private controlled i16 payload, not public job512");
static_assert(offsetof(vxnn_i16_job_v1,a_gpu)==24&&offsetof(vxnn_i16_job_v1,b_gpu)==28&&offsetof(vxnn_i16_job_v1,out_gpu)==32&&offsetof(vxnn_i16_job_v1,binding)==72&&offsetof(vxnn_i16_job_v1,reserved)==88,"sealed relocation and identity offsets");
int vxnn_i16_validate_job(const vxnn_i16_job_v1*);
int vxnn_i16_matmul(const int16_t*,size_t,const int16_t*,size_t,uint32_t,uint32_t,uint32_t,int64_t*,size_t,const mosaic_s8_ops*);
int vxnn_i48_bias(const int64_t*,size_t,const int32_t*,size_t,uint32_t,uint32_t,int64_t*,size_t,uint32_t);
int vxnn_i48_relu(const int64_t*,size_t,int64_t*,size_t,uint32_t);
