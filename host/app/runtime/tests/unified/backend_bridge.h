#ifndef TEST_BACKEND_BRIDGE_H
#define TEST_BACKEND_BRIDGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void *backend_bridge_create(const char *);
void backend_bridge_destroy(void *);
int backend_bridge_call(void *,unsigned long,void *);
void backend_bridge_entry(void *,unsigned char out[32]);
uint64_t backend_bridge_live(void *);
unsigned backend_bridge_starts(void *);
int backend_bridge_staging(void *);
void backend_bridge_finish(void *,int);
void backend_bridge_interrupt_wait(void *,int);
void backend_bridge_fault_submit(void *,int);
void backend_bridge_fault_ack(void *,int);
void backend_bridge_identity_change(void *);
#ifdef __cplusplus
}
#endif
#endif
