#ifndef VXNN_MOSAIC_PROFILE_OPS_H
#define VXNN_MOSAIC_PROFILE_OPS_H
#include "mosaic_wire.h"
#include "mosaic_work_layout_v1.h"
#include <string.h>
// Non-cryptographic metadata/error-detection tag. Address relocations are the
// only omitted words; matching Linux cookie/epoch/retirement remains mandatory.
static inline uint32_t mosaic_profile_argument_tag(const VxnnMosaicArgs&a){
 const auto*p=reinterpret_cast<const unsigned char*>(&a);uint32_t hash=2166136261u;
 for(unsigned i=0;i<sizeof(a);++i)if(i<16||i>=48){hash^=p[i];hash*=16777619u;}
 return hash?hash:1;
}
static inline void mosaic_profile_initialize(MosaicProfileRecordV1*p,const VxnnMosaicArgs&a,const MosaicWorkLayoutV1&l,uint32_t core){
 memset(p,0,sizeof(*p));p->header={MOSAIC_PROFILE_MAGIC_V1,MOSAIC_PROFILE_VERSION_V1,256,l.layout_version,a.opcode,core,a.first,a.count,a.reserved,a.reserved?MOSAIC_TIMER_RV32_CSR64_V1:MOSAIC_TIMER_NONE_V1,l.schedule_version,l.weight_layout_version,MOSAIC_PROFILE_STARTED_V1,0,0,mosaic_profile_argument_tag(a)};
}
#endif
