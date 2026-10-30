#include <stdarg.h>
#include <stdio.h>
#include "host_platform.h"
#include "xil_io.h"
#include "xil_cache.h"
#include "xil_printf.h"
uint32_t host_mmio_read(uint64_t a) { return Xil_In32((UINTPTR)a); }
void host_mmio_write(uint64_t a,uint32_t v) { Xil_Out32((UINTPTR)a,v); }
uint32_t host_ps_read(uint64_t a) { return *(volatile uint32_t *)(UINTPTR)a; }
void host_ps_write(uint64_t a,uint32_t v) { *(volatile uint32_t *)(UINTPTR)a=v; }
void host_flush(uint64_t a,uint32_t n) { Xil_DCacheFlushRange((UINTPTR)a,n); }
void host_invalidate(uint64_t a,uint32_t n) { Xil_DCacheInvalidateRange((UINTPTR)a,n); }
void host_barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }
void host_phase(uint32_t p) { (void)p; }
void host_log(const char *fmt,...) {
    char line[512]; va_list args; va_start(args,fmt);
    vsnprintf(line,sizeof(line),fmt,args); va_end(args); xil_printf("%s",line);
}
int main(void) { return host_main(); }
