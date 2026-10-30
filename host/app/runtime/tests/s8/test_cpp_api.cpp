#include <vxl/s8.h>
#include <cassert>
#include <cstdio>
int main() {
 assert(vxl_s8_guard_byte(3,0,0,0)==0xa6);
 assert(vxl_s8_job_crc(nullptr)==0);
 std::puts("S8_CPP_CALLS_REAL_C_API_PASS");
}
