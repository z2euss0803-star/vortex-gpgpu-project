#include "image_pipeline.h"
#include "host_platform.h"
static uint32_t crc_table[256];static int crc_ready;
static void crc_init(void){if(crc_ready)return;for(uint32_t i=0;i<256;i++){uint32_t c=i;for(unsigned k=0;k<8;k++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);crc_table[i]=c;}crc_ready=1;}
uint32_t image_crc32(const uint8_t *p,uint32_t n){crc_init();uint32_t c=~0u;for(uint32_t i=0;i<n;i++)c=(c>>8)^crc_table[(c^p[i])&255u];return ~c;}
uint32_t image_crc_region(uint32_t a,uint32_t n){crc_init();uint32_t c=~0u;for(uint32_t i=0;i<n;i+=4){uint32_t v=host_ps_read(a+i);for(unsigned k=0;k<4&&i+k<n;k++)c=(c>>8)^crc_table[(c^(v>>(8*k)))&255u];}return ~c;}
