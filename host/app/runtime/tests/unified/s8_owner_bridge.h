#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void*s8_owner_bridge_create(const char*,const char*,const unsigned char[32]);
int s8_owner_bridge_call(void*,unsigned long,void*);
unsigned s8_owner_bridge_starts(void*);unsigned s8_owner_bridge_acks(void*);unsigned s8_owner_bridge_live(void*);
void s8_owner_bridge_arithmetic(void*);
void s8_owner_bridge_corrupt_guard(void*);void s8_owner_bridge_destroy(void*);
void s8_owner_bridge_neural_validate(void*,int(*)(const void*));
void s8_owner_bridge_integer_compute(void*,int(*)(const void*,const void*,const void*,void*));
void s8_owner_bridge_before_prepare(void*,void(*)(void*),void*);
unsigned s8_owner_bridge_neural_jobs(void*);unsigned s8_owner_bridge_image_jobs(void*);
#ifdef __cplusplus
}
#endif
