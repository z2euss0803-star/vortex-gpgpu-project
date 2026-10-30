#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace vxunified { namespace detail {
inline void clear_image_shadow(uint8_t* dst,std::size_t bytes) {
 if(bytes)std::memset(dst,0,bytes);
}
// Preserve the existing opaque RGBA background, including unaligned tails.
inline void fill_image_background(uint8_t* dst,std::size_t bytes) {
 const uint8_t pixel[4]={7,16,30,255};
 if(!bytes)return;
 std::size_t filled=bytes<4?bytes:4;
 std::memcpy(dst,pixel,filled);
 // Each source range ends before its destination: no overlap, aliasing,
 // alignment or endian assumptions. Doubling preserves every RGBA byte.
 while(filled<bytes){
  const auto remaining=bytes-filled;
  const auto copied=filled<remaining?filled:remaining;
  std::memcpy(dst+filled,dst,copied);
  filled+=copied;
 }
}
}}
