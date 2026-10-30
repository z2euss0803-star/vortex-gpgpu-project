/* S8-only replacements for SDK optional reporting; no UART/printf/MPM CSR.
 * vx_start.S still preserves the real main return, stores it to the private
 * CRT exit slot, fences, then executes tmc0. */
void vx_perf_dump(void){__asm__ volatile("fence iorw, iorw":::"memory");}
/* The unmodified SDK references vx_printf only on a rejected launch shape.
 * Its caller already returns failure. This bounded link stub cannot print. */
int vx_printf(const char *format,...){(void)format;return -1;}
