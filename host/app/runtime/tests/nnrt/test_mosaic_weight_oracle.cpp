#include <vxnn/mosaic_weight_layout.hpp>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace vxnn;
static unsigned checks;
static void ck(bool v,const char*s){++checks;if(!v){std::fprintf(stderr,"WEIGHT_ORACLE_ASSERT %s check=%u\n",s,checks);std::exit(74);}}
static void put(std::vector<uint8_t>&a,size_t p,unsigned value){a[p]=uint8_t(value);a[p+1]=uint8_t(value>>8);}
int main(){
 std::vector<uint8_t> original(3*32*2),padded(32*16,0);
 for(unsigned row=0;row<3;row++)for(unsigned k=0;k<32;k++){put(original,(row*32+k)*2,row*1000+k+1);put(padded,k*16+row*2,row*1000+k+1);}
 auto result=verify_mosaic_repacked_weights({original.data(),original.size()},{padded.data(),padded.size()},3,32);
 if(!result){std::fprintf(stderr,"EXPECTED_RED_INDEPENDENT_WEIGHT_ORACLE_MISSING %s\n",result.error->detail.c_str());return 73;}
 const auto frozen=original,expected=padded;
 for(size_t i=0;i<padded.size();i++){auto wrong=padded;wrong[i]^=1;ck(!verify_mosaic_repacked_weights({original.data(),original.size()},{wrong.data(),wrong.size()},3,32),"every data/padding byte mutation refused");}
 ck(original==frozen&&padded==expected,"oracle never writes immutable inputs");
 std::vector<uint8_t> eight(8*32*2),eight_packed(eight.size());
 for(unsigned row=0;row<8;row++)for(unsigned k=0;k<32;k++){put(eight,(row*32+k)*2,row*1000+k+1);put(eight_packed,k*16+row*2,row*1000+k+1);}
 ck(bool(verify_mosaic_repacked_weights({eight.data(),eight.size()},{eight_packed.data(),eight_packed.size()},8,32)),"all eight actual rows exact");
 ck(!verify_mosaic_repacked_weights({eight.data(),eight.size()},{eight.data(),eight.size()},8,32),"same byte extent does not imply layout2");
 ck(!verify_mosaic_repacked_weights({original.data(),original.size()-1},{padded.data(),padded.size()},3,32),"short original segment refused");ck(!verify_mosaic_repacked_weights({original.data(),original.size()},{padded.data(),padded.size()-1},3,32),"short derived segment refused");
 ck(!verify_mosaic_repacked_weights({nullptr,original.size()},{padded.data(),padded.size()},3,32),"null source refused");
 for(unsigned co:{0u,129u})ck(!verify_mosaic_repacked_weights({original.data(),original.size()},{padded.data(),padded.size()},co,32),"invalid channels refused");
 for(unsigned kp:{0u,31u,2624u})ck(!verify_mosaic_repacked_weights({original.data(),original.size()},{padded.data(),padded.size()},3,kp),"invalid kpad refused");
 std::printf("MOSAIC_INDEPENDENT_WEIGHT_LAYOUT2_DATA_AND_ZERO_TAIL_ORACLE_PASS checks=%u\n",checks);return 0;
}
