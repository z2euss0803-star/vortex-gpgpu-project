#include "device/vxnn/hunet_wire.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
extern "C" int arena_native_fixture(const char*,const char*,int,uint32_t*);
int main(int n,char**v){if(n!=3)return 2;uint32_t raw[32]{};VxnnHunetArgs a{};VxnnHunetShape shape{};
 assert(!arena_native_fixture(v[1],v[2],0,raw));std::memcpy(&a,raw,128);assert(vxnn_hunet_validate(&a,nullptr,0,true,&shape)==-ERANGE);
 assert(!arena_native_fixture(v[1],v[2],1,raw));std::memcpy(&a,raw,128);assert(!vxnn_hunet_validate(&a,nullptr,0,true,&shape));assert(shape.required[VXH_X]==16&&shape.required[VXH_Y]==4&&shape.required[VXH_WORK]==16);
 puts("U12_FROZEN_CODE_PRIVATE_RESERVATIONS_PREFIX_RELOCATION_AND_PIN_RETIREMENT_PASS_HOST_NO_GPU gpu_raw_cycles=null");
}
