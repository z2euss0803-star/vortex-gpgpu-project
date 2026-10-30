#include "../../device/s8/phase.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
struct Clock{unsigned calls=0;bool unstable=false;};
static uint32_t hi(void *p){auto &c=*static_cast<Clock *>(p);return c.unstable?c.calls++:1;}
static uint32_t lo(void *p){static_cast<Clock *>(p)->calls++;return 7;}
int main(){
 Clock c;s8_cycle_io io={&c,hi,lo};uint64_t raw=0;assert(!s8_read_mcycle_from(&io,&raw)&&raw==((1ull<<32)|7));
 c.unstable=true;c.calls=0;assert(s8_read_mcycle_from(&io,&raw)==-EAGAIN&&c.calls==12);
 vxl_s8_phase_v1 phase={};assert(!s8_phase_init(&phase,3,2));
 for(unsigned i=0;i<12;++i)assert(!s8_phase_store(&phase,i,100+i));
 assert(phase.count==12&&phase.valid_mask==0xfff&&phase.width==44&&phase.core==3&&phase.warp==2);
 assert(s8_phase_store(&phase,12,112)<0);assert(s8_phase_store(&phase,2,113)<0);
 puts("S8_PHASE_HLH_FOUR_ATTEMPTS_AND_TWELVE_RAW_EVENTS_PASS SOFTWARE_ONLY");
}
