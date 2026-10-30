#pragma once
#include <stddef.h>
#include <stdint.h>
namespace vxnn_device {
inline bool span(const void*p,size_t n,size_t alignment){uintptr_t a=reinterpret_cast<uintptr_t>(p);return p&&n&&a%alignment==0&&n<=~uintptr_t(0)-a;}
// Call only after both finite spans have been checked.
inline bool overlap(const void*a,size_t an,const void*b,size_t bn){uintptr_t x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);return x<y+bn&&y<x+an;}
inline bool matrix(uint32_t m,uint32_t n){return m&&m<=64&&n&&n<=64&&m%8==0&&n%8==0;}
inline bool signed48(int64_t v){return v>=-(INT64_C(1)<<47)&&v<(INT64_C(1)<<47);}
}
