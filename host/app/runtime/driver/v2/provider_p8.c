#include <vxl/v2/provider.h>
#include "provider_common.h"
int vxl_v2_p8_admit(const struct vxl_v2_boot_identity *b,const struct vxl_v2_catalog_entry *e){
 return fixed_admit(b,e,2,"vortex,w4-graphics-p8-v1","22343e02d30978412eeb245474fb2e272bf19fe1da08ec00c0e13e24f005b012","faeaddb98d5d39a4e787c6b2aaf559d6a6163191a1cc8eb18331624b896290dc","08183adeb93ec2ffe3c8c5643813daae12365f61efb95a0d985f04660c340536");
}
