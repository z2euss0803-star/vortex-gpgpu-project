#include "../../tools/cnn_reference.h"
#include <vxl/cnn.h>
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static void payload(const std::string& name, int32_t* dest, unsigned n) {
 std::ifstream f(name, std::ios::binary);
 assert(f.good());
 std::vector<uint32_t> words(32+((n*4+63)/64)*16);
 f.read(reinterpret_cast<char*>(words.data()), words.size()*4);
 assert(static_cast<size_t>(f.gcount())==words.size()*4 && f.peek()==EOF);
 for(unsigned i=0;i<16;i++) assert(words[i]==0x55aa55aa && words[words.size()-16+i]==0xaa55aa55);
 for(unsigned i=16+n;i<words.size()-16;i++) assert(words[i]==0xc0def00d);
 // Tests run on the little-endian Linux hosts specified by this contract.
 std::memcpy(dest,words.data()+16,n*4);
}

int main(int argc,char** argv) {
 static_assert(sizeof(vxl_cnn_args)==64,"CNN args wire size");
 CnnWeights w{}; CnnOutputs out{}; int32_t x[64];
 std::fill(x,x+64,1); std::fill(w.conv,w.conv+18,1); std::fill(w.fc,w.fc+36,1);
 assert(cnn_reference(x,w,&out)==0);
 for(auto v:out.z) assert(v==9);
 for(auto v:out.r) assert(v==9);
 for(auto v:out.p) assert(v==9);
 assert(out.y[0]==162 && out.y[1]==162);
 std::fill(x,x+64,-1); assert(cnn_reference(x,w,&out)==0);
 for(auto v:out.z) assert(v==-9);
 for(auto v:out.r) assert(v==0);
 for(auto v:out.p) assert(v==0);
 assert(out.y[0]==0 && out.y[1]==0);
 std::fill(x,x+64,0); x[10]=3;
 for(unsigned i=0;i<18;i++) w.conv[i]=static_cast<int32_t>(i+1);
 assert(cnn_reference(x,w,&out)==0);
 assert(out.z[0]==18 && out.z[36]==45 && out.z[14]==0);
 std::fill(x,x+64,4); std::fill(w.conv,w.conv+18,INT32_MAX);
 assert(cnn_reference(x,w,&out)==-ERANGE);
 assert(cnn_reference(x,w,nullptr)==-EINVAL);
 assert(argc==2);
 for(const char* name: {"main","zero","negative","positive","checker"}) {
  std::string dir=std::string(argv[1])+"/"+name+"/";
  payload(dir+"x.bin",x,64); payload(dir+"wconv.bin",w.conv,18); payload(dir+"bconv.bin",w.conv_bias,2);
  payload(dir+"wfc.bin",w.fc,36); payload(dir+"bfc.bin",w.fc_bias,2);
  CnnOutputs expected{};
  payload(dir+"z.expected.bin",expected.z,72); payload(dir+"r.expected.bin",expected.r,72);
  payload(dir+"p.expected.bin",expected.p,18); payload(dir+"y.expected.bin",expected.y,2);
  assert(cnn_reference(x,w,&out)==0);
  assert(std::memcmp(&out,&expected,sizeof(out))==0);
 }
 std::puts("CPU_REFERENCE_CPP_PYTHON_5_CASES_PASS_NOT_GPU");
}
