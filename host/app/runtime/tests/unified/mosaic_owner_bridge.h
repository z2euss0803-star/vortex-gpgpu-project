#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void*mosaic_owner_bridge_create(const char*,const char*,const char*,const unsigned char[32]);
int mosaic_owner_bridge_call(void*,unsigned long,void*);
unsigned mosaic_owner_bridge_starts(void*);unsigned mosaic_owner_bridge_acks(void*);
unsigned mosaic_owner_bridge_live(void*);void mosaic_owner_bridge_corrupt_guard(void*);
void mosaic_owner_bridge_destroy(void*);
void mosaic_owner_bridge_expect_input(void*,const unsigned char[32]);
unsigned mosaic_owner_bridge_checked_input(void*);
#ifdef __cplusplus
}
#endif
