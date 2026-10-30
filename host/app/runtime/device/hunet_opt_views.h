#pragma once
#include <stdint.h>
struct OptViewSegment {uint32_t dst_first,count,source_gpu,source_first,source_ci,source_h,source_w;int32_t dy,dx;};
struct OptView {uint32_t block_id,node_index,channels,height,width,segment_count;const OptViewSegment *segments;};
struct OptViewAddress {uint32_t gpu;bool valid;};
static inline OptViewAddress opt_view_address(const OptView&view,uint32_t channel,uint32_t y,uint32_t x){
 if(!view.segments||!view.segment_count||view.segment_count>32||channel>=view.channels||y>=view.height||x>=view.width)return {0,false};
 for(uint32_t i=0;i<view.segment_count;i++){
  const auto&s=view.segments[i];if(channel<s.dst_first||uint64_t(channel)-s.dst_first>=s.count)continue;
  uint64_t c=uint64_t(s.source_first)+channel-s.dst_first;int64_t row=int64_t(y)+s.dy,col=int64_t(x)+s.dx;
  if(!s.source_h||!s.source_w||c>=s.source_ci||row<0||col<0||row>=s.source_h||col>=s.source_w||uint64_t(s.source_ci)*s.source_h>UINT32_MAX/4)return {0,false};
  uint64_t plane=uint64_t(s.source_ci)*s.source_h;if(plane*s.source_w>UINT32_MAX/4)return {0,false};
  uint64_t address=uint64_t(s.source_gpu)+((c*s.source_h+uint64_t(row))*s.source_w+uint64_t(col))*4;
  if((address&3)||address>UINT32_MAX-3)return {0,false};
  return {uint32_t(address),true};
 }
 return {0,false};
}
