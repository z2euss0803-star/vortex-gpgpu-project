#include <vxl/mosaic_assets.h>
#include <vxl/hunet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "mosaic_fixture.h"
static vxl_u64 le64(const vxl_u8 *p){vxl_u64 value=0;for(unsigned i=0;i<8;i++)value|=(vxl_u64)p[i]<<(8*i);return value;}
static vxl_u32 le32(const vxl_u8 *p){return (vxl_u32)le64((const vxl_u8[8]){p[0],p[1],p[2],p[3],0,0,0,0});}
static void put32(vxl_u8 *p,vxl_u32 v){for(unsigned i=0;i<4;i++)p[i]=(vxl_u8)(v>>(8*i));}
static void put64(vxl_u8 *p,vxl_u64 v){for(unsigned i=0;i<8;i++)p[i]=(vxl_u8)(v>>(8*i));}
static void resign(vxl_u8 *p,size_t n,struct mosaic_manifest *m){vxl_hunet_hash(p+256,n-256,p+176);vxl_hunet_hash(p,n,m->package_sha);}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long end=ftell(f);assert(end>=6400&&end<=MOSAIC_ASSET_MAX);rewind(f);
 size_t n=(size_t)end;vxl_u8 *storage=malloc(n+1),*copy=malloc(n);assert(storage&&copy);vxl_u8 *p=storage+1;
 assert(fread(p,1,n,f)==n);assert(!fclose(f));memcpy(copy,p,n);
 struct mosaic_manifest m=fixture_manifest;
 /* Functional RED: valid unaligned full package must be accepted. */
 assert(mosaic_assets_validate(p,n,&m)==0);
 assert(mosaic_assets_validate(NULL,n,&m)<0);assert(mosaic_assets_validate(p,n,NULL)<0);
 assert(mosaic_assets_validate(p,n-1,&m)<0);assert(mosaic_assets_validate(p,(size_t)-1,&m)<0);
 p[n-1]^=1;assert(mosaic_assets_validate(p,n,&m)<0);memcpy(p,copy,n);
 m.source_sha[0][0]^=1;assert(mosaic_assets_validate(p,n,&m)<0);m=fixture_manifest;
 const size_t positions[]={208,256+32,256+4,256+64+32,256+15*384+64+3*64};
 for(unsigned i=0;i<sizeof(positions)/sizeof(positions[0]);i++){
  memcpy(p,copy,n);m=fixture_manifest;p[positions[i]]^=1;resign(p,n,&m);assert(mosaic_assets_validate(p,n,&m)<0);
 }
 memcpy(p,copy,n);m=fixture_manifest;put64(p+256+64+8,~(vxl_u64)63);resign(p,n,&m);assert(mosaic_assets_validate(p,n,&m)<0);
 memcpy(p,copy,n);m=fixture_manifest;put64(p+256+64+16,~(vxl_u64)0);resign(p,n,&m);assert(mosaic_assets_validate(p,n,&m)<0);
 /* Reject malformed numerical payload even after recomputing all checksums. */
 for(unsigned which=0;which<4;which++){
  memcpy(p,copy,n);m=fixture_manifest;size_t entry=256+64+(which==2?2:which==3?1:0)*64;
  vxl_u64 offset=le64(p+entry+8),bytes=le64(p+entry+16);assert(offset+bytes<=n);
  if(which==0){p[offset]=0;p[offset+1]=128;}
  if(which==1){size_t pad=2*le32(p+256+16);assert(pad+2<=bytes);p[offset+pad]=1;p[offset+pad+1]=0;}
  if(which==2)put32(p+offset,0);
  if(which==3)put32(p+offset,0x7fc00000u);
  vxl_hunet_hash(p+offset,bytes,p+entry+32);resign(p,n,&m);assert(mosaic_assets_validate(p,n,&m)<0);
 }
 memcpy(p,copy,n);assert(!mosaic_assets_validate(p,n,&fixture_manifest));
 free(copy);free(storage);puts("MOSAIC_NATIVE_ASSET_C_VALIDATION_PASS_NOT_GPU_NOT_BOARD");return 0;
}
