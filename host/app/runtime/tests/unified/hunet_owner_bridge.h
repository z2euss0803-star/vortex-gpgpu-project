#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void*hunet_owner_bridge_create(const char*,const char*,const unsigned char[32]);
int hunet_owner_bridge_call(void*,unsigned long,void*);
unsigned hunet_owner_bridge_starts(void*);unsigned hunet_owner_bridge_acks(void*);
unsigned hunet_owner_bridge_live(void*);void hunet_owner_bridge_corrupt_guard(void*);
void hunet_owner_bridge_destroy(void*);
void hunet_owner_bridge_expect_input(void*,const unsigned char[32]);
unsigned hunet_owner_bridge_checked_input(void*);
#ifdef __cplusplus
}
#endif
