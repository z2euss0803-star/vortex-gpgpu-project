#ifndef MOSAIC_FAKE_S8_H
#define MOSAIC_FAKE_S8_H
#include "tile.h"
struct fake_s8 {
 int64_t sums[64]={};uint32_t k=0,accepted=0,bitmap=0;
 unsigned begins=0,steps=0,reads=0,releases=0,fences=0,polls=0;
 int fail_begin=0,fail_step=-1,fail_page=-1,fail_release=0;
 bool bad_begin=false,bad_status=false,never_ready=false,bad_idle=false;
 unsigned ready_after=2,current_polls=0;uint64_t ticks=0;bool active=false;
 mosaic_s8_ops ops();
};
#endif
