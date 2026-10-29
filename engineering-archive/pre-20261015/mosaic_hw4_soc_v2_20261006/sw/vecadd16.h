#ifndef VECADD16_H
#define VECADD16_H
#include <stdint.h>
/* Exact words in kernel/vecadd16.hex; encoded by scripts/make_kernel.py.
 * Its absolute common_entry address is 0x10000010. Do not relocate. */
static const uint32_t vecadd16_code[] = {
  0x00400293u,0x10000337u,0x01030313u,0x0062900bu,
  0x00f00293u,0x0002800bu,0x340023f3u,0xf1402473u,
  0x00241413u,0x0003a483u,0x0043a503u,0x0083a583u,
  0x008484b3u,0x00850533u,0x008585b3u,0x0004a603u,
  0x00052683u,0x00d60733u,0x00e5a023u,0x0330000fu,
  0x0000000bu,0x0000006fu
};
#endif
