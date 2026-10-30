#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void*image_owner_bridge_create(const char*,const char*,const unsigned char[32]);
int image_owner_bridge_call(void*,unsigned long,void*);
unsigned image_owner_bridge_starts(void*);unsigned image_owner_bridge_acks(void*);unsigned image_owner_bridge_live(void*);
void image_owner_bridge_arithmetic(void*);
void image_owner_bridge_corrupt_guard(void*);void image_owner_bridge_destroy(void*);
#ifdef __cplusplus
}
#endif
