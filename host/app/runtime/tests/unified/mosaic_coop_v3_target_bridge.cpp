#include "device/vxnn/mosaic_wire_coop_v3.h"
#include <cstring>
extern "C" int mosaic_coop_v3_target_check(const uint32_t *packet){
 VxnnMosaicArgs a;VxnnMosaicShape shape{};std::memcpy(&a,packet,sizeof a);
 return vxnn_mosaic_validate_coop_v3(&a,true,&shape);
}
