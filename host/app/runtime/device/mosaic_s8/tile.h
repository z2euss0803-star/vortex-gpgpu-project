#ifndef MOSAIC_S8_TILE_H
#define MOSAIC_S8_TILE_H
#define MOSAIC_COOP_V3 1
#include <stdint.h>
// Precondition for this native fast path: row is4-byte aligned and lane<4.
// Tile admission checks alignment once before BEGIN; every K row is16 bytes.
static inline uint32_t mosaic_lane_word(const int16_t*row,uint32_t lane){
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
 const auto*bytes=static_cast<const unsigned char*>(__builtin_assume_aligned(row,4));uint32_t word;__builtin_memcpy(&word,bytes+lane*4,4);return word;
#else
 return uint16_t(row[lane*2])|(uint32_t(uint16_t(row[lane*2+1]))<<16);
#endif
}
struct MosaicProfileRecordV1;
struct mosaic_words4 {uint32_t word[4];};
/* All four owner-warp lanes participate in real ISA operations. The injected
 * host model uses participant4 (aggregate), never a hardware evidence claim. */
struct mosaic_s8_ops {
 void *context;
 int (*begin)(void *,uint32_t,mosaic_words4 *);
 int (*step)(void *,const mosaic_words4 *,const mosaic_words4 *);
 int (*read)(void *,uint32_t,mosaic_words4 *);
 int (*release)(void *,mosaic_words4 *);
 void (*fence)(void *);
 uint64_t (*now)(void *);
 uint32_t core,warp,participant,poll_limit;
 uint64_t deadline_ticks;
 int (*broadcast)(void *,int)=nullptr;
 // Optional native K loop; null retains the original injectable scalar path.
 int (*steps)(void *,const int16_t *,const int16_t *,uint32_t)=nullptr;
 // Cooperative mode: all four participants call the same collectives. raw_pages
 // is a distinct shared96-word WORK region, never an alias of packed A/B/ACC.
 int (*read_pages)(void *,uint32_t *)=nullptr;
 int (*synchronize)(void *)=nullptr;
 int (*agree)(void *,int)=nullptr;
 uint32_t *raw_pages=nullptr;
 MosaicProfileRecordV1 *profile=nullptr;
};
int mosaic_tile8(const int16_t *,const int16_t *,uint32_t,int64_t [64],const mosaic_s8_ops *);
int mosaic_unpack48(const uint32_t [96],int64_t [64]);
int mosaic_unpack48_range(const uint32_t [96],int64_t *,uint32_t,uint32_t);
#if defined(__riscv) && (__riscv_xlen==32)
mosaic_s8_ops mosaic_native_s8_ops(uint32_t core,uint32_t warp,uint32_t lane,uint32_t polls);
#endif
#endif
