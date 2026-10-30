#include <vxl/mosaic_host.h>
#include <vxl/hunet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void){
 struct mosaic_program p={0};assert(!mosaic_program_rodata_check(&p));
 p.rodata_bytes=6307840;p.rodata_gpu=0x80110000;p.rodata=calloc(1,p.rodata_bytes);assert(p.rodata);vxl_hunet_hash(p.rodata,p.rodata_bytes,p.rodata_sha);
 assert(!mosaic_program_rodata_check(&p));
 assert(mosaic_program_code_span(&p,0x110000,64));assert(mosaic_program_code_span(&p,0x110000+p.rodata_bytes-64,64));
 assert(!mosaic_program_code_span(&p,0x100040,128));assert(!mosaic_program_code_span(&p,0x110000-16,32));assert(!mosaic_program_code_span(&p,0x110000+p.rodata_bytes,16));
 p.rodata_sha[0]^=1;assert(mosaic_program_rodata_check(&p)<0);p.rodata_sha[0]^=1;
 p.rodata_gpu=0x80100040;assert(mosaic_program_rodata_check(&p)<0);p.rodata_gpu=0x80700000;assert(mosaic_program_rodata_check(&p)<0);
 free((void *)p.rodata);puts("MOSAIC_READONLY_SIDECAR_RANGE_AND_SHA_PASS_NOT_GPU raw_gpu_cycles=null");
}
