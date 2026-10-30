#pragma once
#include <stdint.h>
#include <stddef.h>
#define HUNET_MAGIC UINT32_C(0x314e5548)
typedef struct {
  uint32_t magic;
  uint32_t abi;
  uint32_t bytes;
  uint32_t op;
  uint32_t sequence;
  uint32_t flags;
  uint32_t dtype;
  uint32_t tasks;
  uint32_t x;
  uint32_t w;
  uint32_t bias;
  uint32_t gamma;
  uint32_t beta;
  uint32_t y;
  uint32_t scratch;
  uint32_t aux;
  uint32_t ci;
  uint32_t co;
  uint32_t ih;
  uint32_t iw;
  uint32_t oh;
  uint32_t ow;
  uint32_t k;
  uint32_t nitems;
  uint32_t stride;
  uint32_t pad;
  uint32_t groups;
  uint32_t n_div;
  uint32_t epsilon_bits;
  uint32_t activation;
  uint32_t stage_id;
  uint32_t reserved;
} HunetJobV1;
typedef struct { uint32_t M,N,K,MP,NP,KP,offset,count; } HunetMatrix;
enum HunetOp { HUNET_PACK=1, HUNET_GEMM=2, HUNET_UNPACK=3, HUNET_GN_STATS=4, HUNET_GN_APPLY=5, HUNET_GELU=6, HUNET_SHIFT=7, HUNET_ADD=8, HUNET_GAP=9, HUNET_SIGMOID=10, HUNET_MUL_CHANNEL=11, HUNET_SLICE=12, HUNET_CONCAT=13, HUNET_COPY=14 };
#ifdef __cplusplus
#define HUNET_ASSERT static_assert
#else
#define HUNET_ASSERT _Static_assert
#endif
HUNET_ASSERT(sizeof(HunetJobV1)==128,"job ABI size");
HUNET_ASSERT(offsetof(HunetJobV1,x)==32,"pointer offset");
HUNET_ASSERT(offsetof(HunetJobV1,ci)==64,"dimension offset");
HUNET_ASSERT(offsetof(HunetJobV1,stride)==96,"parameter offset");
static inline int hunet_valid_job(const HunetJobV1 *j) { return j->magic==HUNET_MAGIC && j->abi==1 && j->bytes==128 && j->op>=1 && j->op<=14 && j->reserved==0; }
