#include <vxl/core.h>
#include <assert.h>
#include <stdio.h>
#include "../../vendor/h0/vecadd16.h"
int main(void){vxl_u8 bytes[128]={0};memcpy(bytes,vecadd16_code,sizeof(vecadd16_code));
 assert(!vxl_program_check(bytes,sizeof(bytes),0x10000000));
 assert(vxl_program_check(bytes,sizeof(bytes),0x80000000)==-EACCES);
 assert(vxl_program_check(bytes,16,0x10000000)==-EACCES);
 bytes[0]^=1;assert(vxl_program_check(bytes,sizeof(bytes),0x10000000)==-EACCES);bytes[0]^=1;
 bytes[127]=1;assert(vxl_program_check(bytes,sizeof(bytes),0x10000000)==-EACCES);
 puts("EXACT_DIAGNOSTIC_PROGRAM_IDENTITY_LOW_WRITE_GUARD_PASS");return 0;}
