#include <s8/reference.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
static uint64_t get(FILE *f,unsigned n) {uint64_t r=0;for(unsigned j=0;j<n;++j){int c=fgetc(f);assert(c!=EOF);r|=uint64_t(c)<<(8*j);}return r;}
int main(int argc,char **argv) {
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);char magic[8];assert(fread(magic,1,8,f)==8);
 assert(std::memcmp(magic,"VXLREF1",8)==0);
 assert(get(f,4)==5);
 int64_t out[64];unsigned checked=0;
 for(unsigned q=0;q<5;++q){uint32_t k=get(f,4);std::vector<int16_t>a(8*k),b(8*k);
  for(auto &v:a) { v=int16_t(get(f,2)); }
  for(auto &v:b) { v=int16_t(get(f,2)); }
  assert(s8_reference(a.data(),b.data(),k,out)==0);
  for(unsigned i=0;i<64;++i){uint64_t raw=get(f,8);int64_t expected=raw<=INT64_MAX?int64_t(raw):-1-int64_t(UINT64_MAX-raw);assert(out[i]==expected);++checked;}
 }
 assert(fgetc(f)==EOF);fclose(f);
 std::array<uint8_t,384>raw{};int64_t want[64]={0,-1,-140737488355328LL,140737488355327LL,0x123456789abcLL,-0x123456789abcLL};
 for(unsigned i=0;i<64;++i)for(unsigned j=0;j<6;++j)raw[6*i+j]=uint8_t(uint64_t(want[i])>>(8*j));
 assert(s8_unpack48(raw.data(),out)==0);for(unsigned i=0;i<64;++i)assert(out[i]==want[i]);
 assert(s8_unpack48_checked(raw.data(),383,out)<0);assert(s8_unpack48(nullptr,out)<0);assert(s8_unpack48(raw.data(),nullptr)<0);
 std::vector<int16_t>a(4096*8,-32768),b(a);assert(s8_reference(a.data(),b.data(),4096,out)==0);
 for(auto v:out)assert(v==4398046511104LL);
 assert(s8_reference(a.data(),b.data(),4097,out)<0);assert(s8_reference(nullptr,b.data(),1,out)<0);
 printf("S8_REFERENCE_PASS frozen=%u signed48=64 extreme_k=4096\n",checked);
}
