#include "graph.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <math.h>
#include <fenv.h>
int main(){
 float input[]={.5f,1.5f,2.5f,-.5f,-1.5f,-2.5f,1e30f,-1e30f};int16_t qout[8];
 const int16_t expected_quant[]={0,2,2,0,-2,-2,32767,-32767};
 assert(mosaic_quantize_input(input,8,1,qout)==0);
 for(unsigned i=0;i<8;++i)assert(qout[i]==expected_quant[i]);
 float nan=NAN;assert(mosaic_quantize_input(&nan,1,1,qout)<0);
 assert(mosaic_quantize_input(input,8,0,qout)<0);
 assert(!fesetround(FE_UPWARD));assert(mosaic_quantize_input(input,8,1,qout)<0);assert(!fesetround(FE_TONEAREST));
 int16_t q[]={10,20,30,40};mosaic_layer layer={1,1,2,2,3,1,2,32,q,4};
 std::vector<int16_t> tile(8*32,-1234);
 assert(mosaic_pack_tile(&layer,0,0,tile.data(),tile.size())==0);
 // Virtual nearest-upsample first, reflection in virtual4x4 coordinates.
 const int16_t expected_first[]={10,10,10,10,10,10,10,10,10};
 for(unsigned k=0;k<9;++k)assert(tile[k*8]==expected_first[k]);
 assert(tile[0*8+3]==20);assert(tile[8*8+7]==40);
 for(unsigned k=9;k<32;++k)for(unsigned n=0;n<8;++n)assert(tile[k*8+n]==0);
 assert(mosaic_pack_tile(&layer,8,0,tile.data(),tile.size())==0);
 assert(tile[0]==10);assert(tile[8*8+7]==40);
 assert(mosaic_pack_tile(&layer,16,0,tile.data(),tile.size())<0);
 assert(mosaic_pack_tile(&layer,0,1,tile.data(),tile.size())<0);
 assert(mosaic_pack_tile(&layer,0,0,tile.data(),tile.size()-1)<0);
 layer.quantized_count=3;assert(mosaic_pack_tile(&layer,0,0,tile.data(),tile.size())<0);
 layer.quantized_count=4;layer.upsample=1;
 assert(mosaic_pack_tile(&layer,0,0,tile.data(),tile.size())==0);
 for(unsigned k=0;k<32;++k)for(unsigned n=4;n<8;++n)assert(tile[k*8+n]==0);
 puts("MOSAIC_PACK_SOFTWARE_PASS_NOT_GPU");
}
