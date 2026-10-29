#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "host_platform.h"
#include "host_dpi.h"
static uint32_t access_(int op,uint64_t a,uint32_t value) {
    unsigned int reply=0;
    if(host_bus(op,a,value,&reply)) { fprintf(stderr,"HOST_DPI_TASK_DISABLED\n"); abort(); }
    return reply;
}
uint32_t host_mmio_read(uint64_t a) { return access_(0,a,0); }
void host_mmio_write(uint64_t a,uint32_t v) { (void)access_(1,a,v); }
uint32_t host_ps_read(uint64_t a) { return access_(2,a,0); }
void host_ps_write(uint64_t a,uint32_t v) { (void)access_(3,a,v); }
void host_flush(uint64_t a,uint32_t n) { (void)access_(4,a,n); }
void host_invalidate(uint64_t a,uint32_t n) { (void)access_(5,a,n); }
void host_barrier(void) { (void)access_(6,0,0); }
void host_phase(uint32_t p) { (void)access_(7,0,p); }
void host_log(const char *fmt,...) {
    va_list args;va_start(args,fmt);vprintf(fmt,args);va_end(args);fflush(stdout);
}
int host_entry(int *result) { *result=host_main();return 0; }
