#include <vxl/v2/provider.h>
#include "provider_common.h"
int vxl_v2_s8_admit(const struct vxl_v2_boot_identity *b,const struct vxl_v2_catalog_entry *e){
 if(b && b->provider.flags==4){
  int rc=fixed_admit(b,e,4,"vortex,hw4-s8-joint-v1","1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d","94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1","02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94");
  if(rc)return rc;
  return ((e->namespace_id==1 && e->logical_profile==1 && (e->logical_program==65552 || e->logical_program==65568)) || (e->namespace_id==3 && e->logical_profile==256 && e->logical_program>=512 && e->logical_program<=521) || (e->namespace_id==2 && e->logical_profile==5 && e->logical_program>=9 && e->logical_program<=12))?0:-EXDEV;
 }
 return fixed_admit(b,e,1,"vortex,hw4-s8-demo-v1","1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d","94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1","02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94");
}
