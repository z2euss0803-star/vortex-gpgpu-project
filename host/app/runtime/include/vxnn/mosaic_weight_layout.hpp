#pragma once
#include <vxnn/types.hpp>
#include <cerrno>
namespace vxnn {
inline constexpr const char*mosaic_weight_layout2_name="MB_KPAD_ROW8_I16_LE_V1";
// Independent of the Python repacker and its hashes. Compare every original LE
// byte pair at [co,kpad] with [mb,k,row], then require both tail bytes to be zero.
inline Result<void> verify_mosaic_repacked_weights(Bytes original,Bytes derived,uint32_t co,uint32_t kpad){
 if(!original.data||!derived.data||!co||co>128||kpad<32||kpad>2592||kpad%32||original.size!=uint64_t(co)*kpad*2||derived.size!=uint64_t((co+7)/8)*kpad*16)return Result<void>::failure({EINVAL,"MOSAIC_WEIGHT_LAYOUT","exact original and padded extent required"});
 for(uint32_t mb=0;mb<(co+7)/8;mb++)for(uint32_t k=0;k<kpad;k++)for(uint32_t row=0;row<8;row++){
  const size_t dst=(size_t(mb)*kpad+k)*16+row*2;const uint32_t channel=mb*8+row;
  if(channel<co){const size_t src=(size_t(channel)*kpad+k)*2;if(derived.data[dst]!=original.data[src]||derived.data[dst+1]!=original.data[src+1])return Result<void>::failure({EBADMSG,"MOSAIC_WEIGHT_LAYOUT","derived byte pair differs from frozen original segment"});}
  else if(derived.data[dst]||derived.data[dst+1])return Result<void>::failure({EBADMSG,"MOSAIC_WEIGHT_LAYOUT","padded output-channel row must be zero"});
 }
 return Result<void>::success();
}
}
