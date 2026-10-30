#include "hunet_hybrid_reference.h"
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <cstdint>
/* Independent scalar implementation; never call the production worker. */
int hunet_hybrid_reference1(const float *x,float *y,size_t n){
 if(!x||!y||!n||n>393216||std::fegetround()!=FE_TONEAREST)return -EINVAL;
 uintptr_t a=reinterpret_cast<uintptr_t>(x),b=reinterpret_cast<uintptr_t>(y);size_t bytes=n*4;
 if(a>UINTPTR_MAX-bytes||b>UINTPTR_MAX-bytes||(a<b+bytes&&b<a+bytes))return -EINVAL;
 for(size_t i=0;i<n;i++)if(!std::isfinite(x[i]))return -EDOM;
 for(size_t i=0;i<n;i++)y[i]=(x[i]*0.5f)*(1.0f+std::erf(x[i]/1.4142135623730951f));
 return 0;
}
