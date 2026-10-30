#include <vxunified/detail/image_fill.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(){
 constexpr std::array<uint8_t,4> expected{{7,16,30,255}};
 for(std::size_t bytes:std::vector<std::size_t>{0,1,2,3,4,5,7,8,9,15,16,17,63,64,65,127,128,129,1023,1024,1025,65531,65536,65537}){
  for(std::size_t offset=0;offset<8;++offset){
   std::vector<uint8_t> area(bytes+offset+128,0xa5);
   vxunified::detail::fill_image_background(area.data()+64+offset,bytes);
   for(std::size_t i=0;i<area.size();++i){
    const auto want=i>=64+offset&&i<64+offset+bytes?expected[(i-64-offset)%4]:uint8_t(0xa5);
    if(area[i]!=want){std::fprintf(stderr,"IMAGE_BACKGROUND_BYTE_OR_GUARD_MISMATCH bytes=%zu offset=%zu at=%zu\n",bytes,offset,i);return 1;}
   }
   vxunified::detail::clear_image_shadow(area.data()+64+offset,bytes);
   for(std::size_t i=0;i<area.size();++i){
    const auto want=i>=64+offset&&i<64+offset+bytes?uint8_t(0):uint8_t(0xa5);
    if(area[i]!=want){std::fprintf(stderr,"IMAGE_SHADOW_CLEAR_BYTE_OR_GUARD_MISMATCH bytes=%zu offset=%zu at=%zu\n",bytes,offset,i);return 1;}
   }
  }
 }
 vxunified::detail::fill_image_background(nullptr,0);
 vxunified::detail::clear_image_shadow(nullptr,0);
 puts("U20_IMAGE_BACKGROUND_EXACT_BYTES_UNALIGNED_TAIL_AND_GUARDS_PASS_HOST_NOT_GPU gpu_raw_cycles=null");
 return 0;
}
