#include <stddef.h>
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void*p8_owner_bridge_create(const char*,const char*,const unsigned char[32]);
int p8_owner_bridge_call(void*,unsigned long,void*);
unsigned p8_owner_bridge_starts(void*);unsigned p8_owner_bridge_acks(void*);unsigned p8_owner_bridge_live(void*);

void p8_owner_bridge_corrupt_guard(void*);void p8_owner_bridge_destroy(void*);
void p8_owner_bridge_oracle(void*,const void*,size_t,const void*,const void*,size_t);
/* HOST-only independent CPU oracle injection before the original C prepare.
 * This is not an RV32/GPU emulator or a production driver callback. */
void p8_owner_bridge_before_prepare(void*,void(*)(void*,unsigned),void*);
void p8_owner_bridge_replace_oracle(void*,const void*,size_t,const void*,const void*,size_t);
void p8_owner_bridge_bad_projection(void*);
void p8_owner_bridge_bad_pixel(void*,int);
void p8_owner_bridge_enable_fast(void*); /* HOST C capability only, no GPU */
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
void p8_owner_bridge_expected_frame(void*,const void*const[10],const size_t[10],unsigned);
#ifdef __cplusplus
}
#endif
