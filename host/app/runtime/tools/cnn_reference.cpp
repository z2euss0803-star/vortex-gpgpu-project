#include "cnn_reference.h"
#include <cerrno>
#include <climits>
static bool i32(int64_t v) { return v>=INT32_MIN && v<=INT32_MAX; }
int cnn_reference(const int32_t input[64], const CnnWeights& w, CnnOutputs* out) {
 if(!input || !out) return -EINVAL;
 CnnOutputs tmp{};
 // Independent output-major scalar loops; no GPU kernel code is reused here.
 for(unsigned pos=0;pos<72;pos++) {
  unsigned c=pos/36, row=(pos%36)/6, col=pos%6;
  int64_t acc=w.conv_bias[c];
  for(unsigned k=0;k<9;k++) {
   acc+=static_cast<int64_t>(input[(row+k/3)*8+col+k%3])*w.conv[c*9+k];
   if(!i32(acc)) return -ERANGE;
  }
  tmp.z[pos]=static_cast<int32_t>(acc);
  tmp.r[pos]=acc>0?static_cast<int32_t>(acc):0;
 }
 for(unsigned pos=0;pos<18;pos++) {
  unsigned c=pos/9, row=(pos%9)/3, col=pos%3;
  int32_t largest=tmp.r[c*36+row*12+col*2];
  for(unsigned k=1;k<4;k++) {
   int32_t sample=tmp.r[c*36+(row*2+k/2)*6+col*2+k%2];
   if(sample>largest) largest=sample;
  }
  tmp.p[pos]=largest;
 }
 for(unsigned o=0;o<2;o++) {
  int64_t acc=w.fc_bias[o];
  for(unsigned j=0;j<18;j++) {
   acc+=static_cast<int64_t>(tmp.p[j])*w.fc[o*18+j];
   if(!i32(acc)) return -ERANGE;
  }
  tmp.y[o]=static_cast<int32_t>(acc);
 }
 *out=tmp;
 return 0;
}
