#ifndef HOST_PLATFORM_H
#define HOST_PLATFORM_H
#include <stdint.h>
/* Backends implement access only. Test decisions live in host_logic.c. */
uint32_t host_mmio_read(uint64_t address);
void host_mmio_write(uint64_t address, uint32_t value);
uint32_t host_ps_read(uint64_t address);
void host_ps_write(uint64_t address, uint32_t value);
void host_flush(uint64_t address, uint32_t bytes);
void host_invalidate(uint64_t address, uint32_t bytes);
void host_barrier(void);
void host_log(const char *format, ...);
void host_phase(uint32_t phase);
int host_main(void);
#endif
