#include "admission.h"
int s8_device_admit(const vxl_s8_job_v1 *j){
 if(!j||j->magic!=VXL_S8_JOB_MAGIC||j->version!=1||j->bytes!=128||!j->generation||!j->sequence||j->sequence==0xffffffffu)return 1;
 if((j->core_mask!=1&&j->core_mask!=2&&j->core_mask!=4&&j->core_mask!=8&&j->core_mask!=15)||j->owner_warp>3)return 2;
 if(j->a_base!=0x81000040||j->b_base!=0x81200040||j->input_slot_stride!=0x20000||j->output_base!=0x81400000||j->output_stride!=0x1000||j->identity_base!=0x81410000||j->control_base!=0x81411000||j->spin_limit!=65536)return 3;
 for(unsigned i=0;i<5;++i)if(j->reserved[i])return 4;
 unsigned k=0,phase=0,id=j->case_id;
 if(id==1||id==2||id==4||id==5)k=0;
 else if(id==3)k=8;
 else if(id>=101&&id<=105){const unsigned ks[]={1,2,3,2592,4096};k=ks[id-101];phase=1;}
 else if(id>=110&&id<=115){const unsigned ks[]={1,3,3,4096,4096,3};k=ks[id-110];phase=1;}
 else if(id==120){k=3;phase=1;if(j->core_mask!=15)return 5;}
 else {unsigned f=id>>8,v=id&255,n=(f==201||f==204||f==208)?2:f==203?7:1;if(f<201||f>209||v>=n)return 5;k=2;}
 if(j->k!=k||(j->flags&&(!phase||j->flags!=1)))return 6;
 unsigned run=0,tag=0;for(unsigned i=0;i<16;++i){run|=j->run_id[i];tag|=j->binding_tag[i];}if(!run||!tag)return 7;
 const auto *bytes=reinterpret_cast<const unsigned char *>(j);unsigned crc=0xffffffffu;
 for(unsigned i=0;i<128;++i){crc^=(i>=104&&i<108)?0:bytes[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));}
 return (crc^0xffffffffu)==j->args_crc32?0:8;
}
