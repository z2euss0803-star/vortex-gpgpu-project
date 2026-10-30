#include <vxl/mosaic_numeric.h>
#include <cerrno>
#include <cfenv>
#include <cmath>
int mosaic_quantize_rne(const float *x,size_t n,float inverse,int16_t *out){
 if(!x||!out||!n||!std::isfinite(inverse)||inverse<=0||std::fegetround()!=FE_TONEAREST)return -EINVAL;
 for(size_t i=0;i<n;i++)if(!std::isfinite(x[i]))return -EDOM;
 for(size_t i=0;i<n;i++){
  volatile float scaled=x[i]*inverse;
  float rounded=std::nearbyintf(scaled);
  out[i]=rounded>=32767?32767:rounded<=-32767?-32767:static_cast<int16_t>(rounded);
 }
 return 0;
}
int mosaic_dot_i64(const int16_t *a,const int16_t *b,size_t k,int64_t *out){
 if(!a||!b||!out||!k||k>4096)return -EINVAL;
 int64_t accumulator=0;
 for(size_t i=0;i<k;i++)accumulator+=static_cast<int64_t>(a[i])*static_cast<int64_t>(b[i]);
 *out=accumulator;return 0;
}
int mosaic_restore_f32(int64_t q,float sw,float sa,float bias,float *out){
 if(!out||q<-(INT64_C(1)<<47)||q>=(INT64_C(1)<<47)||!std::isfinite(sw)||!std::isfinite(sa)||!std::isfinite(bias)||sw<=0||sa<=0||std::fegetround()!=FE_TONEAREST)return -EINVAL;
 volatile float converted=static_cast<float>(q);
 volatile float weighted=converted*sw;
 volatile float scaled=weighted*sa;
 volatile float value=scaled+bias;
 if(!std::isfinite(value))return -ERANGE;
 *out=value;return 0;
}
