#include "entry.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cerrno>
int main(){
 mosaic_entry_catalog cat={};cat.width=256;cat.height=256;
 assert(!mosaic_entry_layout(cat.width,cat.height,&cat.layout));
 memset(cat.layout_tag,1,16);memset(cat.graph_tag,2,16);
 for(unsigned i=0;i<16;i++){
  mosaic_layer l;assert(!mosaic_layer_geometry(i,256,256,&l));
  auto &p=cat.parameters[i];p.weights=reinterpret_cast<const int16_t *>(uintptr_t(0x80802000+i*64));p.weight_count=size_t(l.co)*l.kpad;
  p.coeff=reinterpret_cast<const float *>(uintptr_t(0x80f00000+i*64));p.coeff_count=l.co*2;p.sa=.125f;
  p.gamma=reinterpret_cast<const float *>(uintptr_t(0x80f10000+i*64));p.beta=reinterpret_cast<const float *>(uintptr_t(0x80f20000+i*64));
 }
 mosaic_job_v1 j={};j.magic=0x31434d56;j.version=1;j.bytes=128;j.run_id[0]=3;j.binding_tag[0]=4;j.generation=5;j.sequence=7;j.op=1;j.layer=0;j.tile_count=1;j.core_mask=15;j.input_epoch=1;memcpy(j.layout_tag,cat.layout_tag,16);memcpy(j.graph_tag,cat.graph_tag,16);j.crc=mosaic_job_crc(&j);
 mosaic_device_context c={};
 assert(!mosaic_entry_prepare(&j,&cat,1,2,&c));assert(c.core==2&&c.width==256&&c.phase_index==0&&c.expected.sequence==7);
 assert(c.memory.fp[0]==reinterpret_cast<float *>(uintptr_t(cat.layout.region[MOSAIC_FP_A].address)));
 assert(c.memory.norm[0].count==65536&&c.memory.norm[3].count==65536);
 assert(reinterpret_cast<uintptr_t>(c.memory.norm[3].lo)+65536*4<=cat.layout.region[MOSAIC_NORM_TREE].address+cat.layout.region[MOSAIC_NORM_TREE].bytes);
 assert(c.memory.tile_capacity>=8*2592&&c.parameters[0].weight_count==32*256);
 unsigned char identity[64];assert(!mosaic_entry_metadata(&j,2,identity));assert(!memcmp(identity,j.run_id,16)&&!memcmp(identity+16,j.binding_tag,16));
 uint32_t fields[8];memcpy(fields,identity+32,32);assert(fields[0]==5&&fields[1]==7&&fields[2]==1&&fields[7]==2);
 for(unsigned mode=0;mode<8;mode++){
  auto bad=j;switch(mode){case 0:bad.magic=0;break;case 1:bad.crc^=1;break;case 2:bad.core_mask=1;break;case 3:bad.tile_count=4097;break;case 4:bad.layout_tag[0]^=1;break;case 5:bad.graph_tag[0]^=1;break;case 6:bad.op=7;break;case 7:bad.generation=0;break;}
  if(mode!=1)bad.crc=mosaic_job_crc(&bad);
  assert(mosaic_entry_prepare(&bad,&cat,1,2,&c)<0);
 }
 assert(mosaic_entry_prepare(&j,&cat,1,4,&c)<0);
 auto bad=j;bad.sequence=0;bad.crc=mosaic_job_crc(&bad);assert(mosaic_entry_prepare(&bad,&cat,1,0,&c)<0);
 bad=j;memset(bad.binding_tag,0,16);bad.crc=mosaic_job_crc(&bad);assert(mosaic_entry_prepare(&bad,&cat,1,0,&c)<0);
 auto badcat=cat;badcat.layout.region[MOSAIC_FP_A].address+=64;assert(mosaic_entry_prepare(&j,&badcat,1,0,&c)<0);
 badcat=cat;badcat.parameters[0].weights=reinterpret_cast<const int16_t *>(uintptr_t(0x10000000));assert(mosaic_entry_prepare(&j,&badcat,1,0,&c)<0);
 assert(!mosaic_entry_layout(1280,960,&cat.layout)&&cat.layout.end<0xfff00000ULL);cat.width=1280;cat.height=960;
 bad=j;bad.tile_count=4097;bad.crc=mosaic_job_crc(&bad);assert(mosaic_entry_prepare(&bad,&cat,1,0,&c)<0);
 bad.tile_count=4096;bad.crc=mosaic_job_crc(&bad);assert(!mosaic_entry_prepare(&bad,&cat,1,0,&c));
 assert(mosaic_entry_layout(1281,960,&cat.layout)<0);
 puts("MOSAIC_DEVICE_ENTRY_ADDRESS_PHASE_METADATA_ADMISSION_PASS_NOT_GPU raw_gpu_cycles=null");
}
