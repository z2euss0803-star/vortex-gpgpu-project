#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct owner512_stats {uint64_t jobs,header_writes,profile_reads,guard_reads,acks,snapshots,traces,injected,buffer_releases,input_packs,weight_prepares,tiles;};
void owner512_config(const char*,unsigned);
void owner512_copy(unsigned,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,unsigned,int);
void owner512_inject(const void*,void*,uint64_t,uint64_t,uint64_t,uint64_t);
void owner512_ack(unsigned);
void owner512_snapshot(const void*);
void owner512_finish_begin(int);
void owner512_finish_end(void);
void owner512_trace(uint64_t,int);
struct owner512_stats owner512_get(void);
void owner512_print(void);
unsigned owner512_current_abi(void);
#ifdef __cplusplus
}
#endif
