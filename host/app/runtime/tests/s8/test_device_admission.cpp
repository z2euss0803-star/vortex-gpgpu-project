#include "../../device/s8/admission.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static void seal(vxl_s8_job_v1 &j){auto *p=reinterpret_cast<unsigned char *>(&j);unsigned crc=~0u;j.args_crc32=0;for(unsigned i=0;i<128;++i){crc^=p[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));}j.args_crc32=~crc;}
int main(){vxl_s8_job_v1 j={};j.magic=VXL_S8_JOB_MAGIC;j.version=1;j.bytes=128;j.case_id=103;j.k=3;j.core_mask=15;j.a_base=0x81000040;j.b_base=0x81200040;j.input_slot_stride=0x20000;j.output_base=0x81400000;j.output_stride=4096;j.identity_base=0x81410000;j.control_base=0x81411000;j.spin_limit=65536;j.generation=2;j.sequence=1;j.run_id[0]=1;j.binding_tag[0]=2;seal(j);assert(!s8_device_admit(&j));
for(unsigned i=0;i<32;++i){auto bad=j;auto *w=reinterpret_cast<unsigned *>(&bad);w[i]^=0x80000000u;if(i>=16&&i<=26)continue;seal(bad);assert(s8_device_admit(&bad));}
auto bad=j;bad.args_crc32^=1;assert(s8_device_admit(&bad));bad=j;std::memset(bad.run_id,0,16);seal(bad);assert(s8_device_admit(&bad));
bad=j;bad.generation=0;seal(bad);assert(s8_device_admit(&bad));bad=j;bad.sequence=0;seal(bad);assert(s8_device_admit(&bad));bad=j;bad.sequence=~0u;seal(bad);assert(s8_device_admit(&bad));
std::puts("S8_DEVICE_INDEPENDENT_FIXED_ADDRESS_CATALOG_CRC_NONCE_ADMISSION_PASS");}
