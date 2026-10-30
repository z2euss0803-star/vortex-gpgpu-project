/* Independent candidate: reuse verified control helpers without changing old sources. */
#include "../vortex23-r4-local/sw/host_platform.h"
#include "../vortex23-r4-local/sw/gpgpu_hw.h"
__attribute__((weak)) void host_service_poll(void){}
static uint32_t cm_service_read(uint64_t address){
 static uint32_t polls;
 if(address==GPU_BASE+GPU_STATUS && !(++polls&255u))host_service_poll();
 return host_mmio_read(address);
}
#define host_mmio_read cm_service_read
#define host_main cm_legacy_host_main
#include "../vortex23-r4-local/suite/host_suite.c"
#undef host_main
#undef host_mmio_read
#undef FB64_BASE
#define FB64_BASE UINT32_C(0x17500000)
#undef V3_DEPTH
#define V3_DEPTH UINT32_C(0x17610000)
#undef V3_TEX
#define V3_TEX UINT32_C(0x19400000)
#ifndef CM_ASSET_HEADER
#define CM_ASSET_HEADER "generated/complex_assets.h"
#endif
#include CM_ASSET_HEADER
static const cm_asset_descriptor *cm_asset=&cm_assets[0];
#define cm_raw (cm_asset->raw)
#define cm_indices (cm_asset->indices)
#define cm_saved_nn_texture (cm_asset->texture)
#define CM_REAL_VERTICES (cm_asset->vertices)
#define CM_REAL_FACES (cm_asset->faces)
#include "generated/complex_kernel.h"
#include "network/fast_crc.h"
#include "theme_palette.h"
static uint32_t cm_theme_palette[10],cm_theme_ready;
static uint32_t cm_material_color(uint32_t triangle){
 if(!cm_asset->material_rgb)return 0;
 uint32_t region=cm_asset->material_rgb[triangle]>>24;
 return cm_theme_palette[region<10u?region:4u];
}
#define CM_RAW 0x15000000u
#define CM_PROJECTED 0x15100000u
#define CM_INDEX 0x15200000u
#define CM_DIR 0x15b10000u
#define CM_REFS 0x15b40000u
#define CM_PRETRI 0x15300000u
#define CM_BG 0x17c00000u
#define CM_SLOTS 16064u
#define CM_ACTIVE_SLOTS ((CM_REAL_VERTICES+63u)&~63u)
#define CM_FACES CM_REAL_FACES
#define CM_REF_CAP 262144u
static const uint32_t cm_bases[10]={FB64_BASE,V3_DEPTH,V3_TEX,CM_RAW,CM_PROJECTED,CM_INDEX,CM_DIR,CM_REFS,CM_PRETRI,CM_BG};
#define CM_WIDTH (cm_asset->width)
#define CM_TEX_SIZE (cm_asset->tex_size)
#define CM_PIXELS (CM_WIDTH*CM_WIDTH)
#define CM_TILES ((CM_WIDTH/4u)*(CM_WIDTH/4u))
static uint32_t cm_sizes[10];
static uint32_t cm_texture_epoch;
static uint32_t cm_committed;
static void cm_configure(void){
 uint32_t values[10]={4u*CM_PIXELS,4u*CM_PIXELS,4u*CM_TEX_SIZE*CM_TEX_SIZE,514048,514048,393216,8u*CM_TILES,1048576,8388608,4u*CM_PIXELS};
 for(uint32_t i=0;i<10;i++)cm_sizes[i]=values[i];
}

static uint32_t cm_projected[128512],cm_directory[32768],cm_refs[CM_REF_CAP],cm_texture[4194304];
static uint32_t cm_colors[262144],cm_depths[262144],cm_owners[262144],cm_active_refs;
static uint32_t cm_pretri[32768*64],cm_last_depth_crc;
static int cm_validate=1;
static uint32_t cm_crc(const uint32_t *p,uint32_t words){return hr_crc32((const uint8_t*)p,4u*words);}

static uint32_t cm_source(uint32_t b,uint32_t i){
 switch(b){case 2:return cm_texture[i];case 3:return cm_raw[i];case 4:return cm_projected[i];case 5:return cm_indices[i];case 6:return cm_directory[i];case 7:return cm_refs[i];case 8:return cm_pretri[i];case 9:return CM_WIDTH==512u?cm_background_high[i]:cm_background[i];default:return UINT32_C(0xdeadbeef);}
}
static void cm_init_buffer(uint32_t b,int poison){
 uint32_t base=cm_bases[b],bytes=cm_sizes[b];
 for(uint32_t side=0;side<2;side++)for(uint32_t i=0;i<16;i++)host_ps_write(side?base+bytes+4u*i:base-64u+4u*i,v3_guard(b,side,i));
 for(uint32_t i=0;i<bytes/4u;i++){
  if(!(i&4095u))host_service_poll();
  uint32_t value=poison?UINT32_C(0xdeadbeef):cm_source(b,i);
  if(b==4&&i>=8u*CM_ACTIVE_SLOTS)value=0;
  host_ps_write(base+4u*i,value);
 }
 host_flush(base-64u,bytes+128u);host_barrier();
}
static int cm_check_buffers(void){
 for(uint32_t b=0;b<10;b++){
  uint32_t base=cm_bases[b],bytes=cm_sizes[b];
  if(cm_validate || b<2 || b==4)host_invalidate(base-64u,bytes+128u);
  else {host_invalidate(base-64u,64);host_invalidate(base+bytes,64);}
  host_barrier();
  for(uint32_t side=0;side<2;side++)for(uint32_t i=0;i<16;i++){
   uint32_t a=side?base+bytes+4u*i:base-64u+4u*i,v=host_ps_read(a),want=v3_guard(b,side,i);
   if(v!=want)return fail("COMPLEX_GUARD",b*32u+side*16u+i,want,v);
  }
  if(b<2 || !cm_validate)continue;
  for(uint32_t i=0;i<bytes/4u;i++){uint32_t v=host_ps_read(base+4u*i),want=cm_source(b,i);
 if(v!=want)return fail("COMPLEX_SOURCE",b*65536u+i,want,v);}
 }
 return 0;
}
static int cm_upload_used(uint32_t b,uint32_t words){
 if(b>=10u||words>cm_sizes[b]/4u)return fail("ACTIVE_UPLOAD_CAPACITY",b,cm_sizes[b<10u?b:0u]/4u,words);
 if(cm_validate){cm_init_buffer(b,0);return 0;}
 for(uint32_t i=0;i<words;i++)host_ps_write(cm_bases[b]+4u*i,cm_source(b,i));
 if(words)host_flush(cm_bases[b],4u*words);
 host_barrier();return 0;
}
static void cm_project_gold(uint32_t frame){
 const uint32_t *m=cm_matrices+12u*frame;
 for(uint32_t i=0;i<8u*CM_SLOTS;i++)cm_projected[i]=0;
 for(uint32_t v=0;v<CM_ACTIVE_SLOTS;v++){
  int32_t xyz[3];for(uint32_t row=0;row<3;row++){
   int64_t s=(int64_t)(int32_t)m[4u*row+3u]*256;
   for(uint32_t j=0;j<3;j++)s+=(int64_t)(int32_t)m[4u*row+j]*(int32_t)cm_raw[8u*v+j];
   xyz[row]=v3_floor256(s);
  }
  uint32_t valid=xyz[2]>=8192 && xyz[2]<=24576;int32_t z=valid?xyz[2]:16384;uint32_t inv=2097152u/(uint32_t)z;
  cm_projected[8u*v]=(uint32_t)((int32_t)(CM_WIDTH*2u)+p4_div_signed(valid?xyz[0]*(int32_t)(CM_WIDTH*15u/4u):0,z));
  cm_projected[8u*v+1]=(uint32_t)((int32_t)(CM_WIDTH*2u)+p4_div_signed(valid?xyz[1]*(int32_t)(CM_WIDTH*15u/4u):0,z));cm_projected[8u*v+2]=inv;
  cm_projected[8u*v+3]=cm_raw[8u*v+3]*inv;cm_projected[8u*v+4]=cm_raw[8u*v+4]*inv;
  int32_t normal[3];
  for(uint32_t row=0;row<3;row++){int64_t n=0;for(uint32_t j=0;j<3;j++)n+=(int32_t)m[4*row+j]*(int32_t)cm_raw[8*v+5+j];normal[row]=v3_floor256(n);}
  int32_t dot=v3_floor256(-96*(int64_t)normal[0]-112*(int64_t)normal[1]-208*(int64_t)normal[2]);if(dot<0)dot=0;if(dot>256)dot=256;
  int32_t half=v3_floor256(-50*(int64_t)normal[0]-58*(int64_t)normal[1]-244*(int64_t)normal[2]);if(half<0)half=0;if(half>256)half=256;
  for(uint32_t k=0;k<4;k++)half=half*half/256;
  cm_projected[8u*v+5]=(92+(uint32_t)dot*156/256)*inv;cm_projected[8u*v+6]=((uint32_t)half*48/256)*inv;cm_projected[8u*v+7]=valid;
 }
}
static int64_t cm_edge(const uint32_t *a,const uint32_t *b,int64_t x,int64_t y){
 return ((int64_t)(int32_t)b[0]-(int32_t)a[0])*(y-(int32_t)a[1])-((int64_t)(int32_t)b[1]-(int32_t)a[1])*(x-(int32_t)a[0]);
}
static int cm_box(uint32_t tid,int32_t box[4]){
 int32_t minx=INT32_MAX,miny=INT32_MAX,maxx=INT32_MIN,maxy=INT32_MIN;
 for(uint32_t j=0;j<3;j++){
  const uint32_t *q=cm_projected+8u*cm_indices[3u*tid+j];
 if(!q[7])return 0;
  int32_t x=(int32_t)q[0],y=(int32_t)q[1];
 if(x<minx)minx=x;
 if(x>maxx)maxx=x;
 if(y<miny)miny=y;
 if(y>maxy)maxy=y;
 }
 if(maxx<2 || maxy<2 || minx>(int32_t)(4u*CM_WIDTH-2u) || miny>(int32_t)(4u*CM_WIDTH-2u))return 0;
 if(minx<2)minx=2;
 if(miny<2)miny=2;
 if(maxx>(int32_t)(4u*CM_WIDTH-2u))maxx=(int32_t)(4u*CM_WIDTH-2u);
 if(maxy>(int32_t)(4u*CM_WIDTH-2u))maxy=(int32_t)(4u*CM_WIDTH-2u);
 box[0]=(minx-2+3)/4;box[1]=(maxx-2)/4;box[2]=(miny-2+3)/4;box[3]=(maxy-2)/4;
 return box[0]<=box[1] && box[2]<=box[3];
}
/* Reject bbox tiles wholly outside any normalized edge; same4-lane tile membership. */
static int cm_tile_intersects(uint32_t tid,uint32_t tx,uint32_t ty){
 const uint32_t *p=cm_pretri+64*tid;if(!p[12])return 0;
 for(uint32_t j=0;j<3;j++){
  int32_t a=(int32_t)p[4*j],b=(int32_t)p[4*j+1],c=(int32_t)p[4*j+2];
  int32_t x=(int32_t)(16*tx)+(a>=0?14:2),y=(int32_t)(16*ty)+(b>=0?14:2);
  if((int64_t)a*x+(int64_t)b*y+c<0)return 0;
 }
 return 1;
}
static int cm_build_tiles(void){
 static uint32_t counts[16384];for(uint32_t tile=0;tile<CM_TILES;tile++)counts[tile]=0;cm_active_refs=0;
 if(cm_validate)for(uint32_t i=0;i<CM_REF_CAP;i++)cm_refs[i]=UINT32_C(0xa55a0000)^i;
 for(uint32_t t=0;t<CM_FACES;t++){
  int32_t b[4];if(!cm_box(t,b))continue;
  for(uint32_t y=(uint32_t)b[2]/4;y<=(uint32_t)b[3]/4;y++)for(uint32_t x=(uint32_t)b[0]/4;x<=(uint32_t)b[1]/4;x++)if(cm_tile_intersects(t,x,y))counts[(CM_WIDTH/4u)*y+x]++;
 }
 for(uint32_t tile=0;tile<CM_TILES;tile++){
  if(counts[tile]>CM_REF_CAP-cm_active_refs)return fail("COMPLEX_REFS_CAPACITY",tile,CM_REF_CAP,cm_active_refs+counts[tile]);
  cm_directory[2*tile]=cm_active_refs;cm_directory[2*tile+1]=counts[tile];cm_active_refs+=counts[tile];counts[tile]=0;
 }
 /* Fill faces in canonical order; two passes avoid tiles*faces bbox recomputation. */
 for(uint32_t t=0;t<CM_FACES;t++){
  int32_t b[4];if(!cm_box(t,b))continue;
  for(uint32_t y=(uint32_t)b[2]/4;y<=(uint32_t)b[3]/4;y++)for(uint32_t x=(uint32_t)b[0]/4;x<=(uint32_t)b[1]/4;x++){
   if(!cm_tile_intersects(t,x,y))continue;
   uint32_t tile=(CM_WIDTH/4u)*y+x,offset=cm_directory[2*tile];
   if(counts[tile]>=cm_directory[2*tile+1] || offset+counts[tile]>=cm_active_refs)return fail("COMPLEX_DIR_FILL",tile,cm_active_refs,offset+counts[tile]);
   cm_refs[offset+counts[tile]++]=t;
  }
 }
 for(uint32_t tile=0;tile<CM_TILES;tile++){
  if(counts[tile]!=cm_directory[2*tile+1])return fail("COMPLEX_DIR_COUNT",tile,cm_directory[2*tile+1],counts[tile]);
  for(uint32_t i=0;i<counts[tile];i++)if(cm_refs[cm_directory[2*tile]+i]>=CM_FACES)return fail("COMPLEX_REF_ID",i,CM_FACES-1,cm_refs[cm_directory[2*tile]+i]);
 }
 return cm_upload_used(6,2u*CM_TILES) || cm_upload_used(7,cm_active_refs);
}
static int cm_build_pretri(void){
 for(uint32_t t=0;t<CM_FACES;t++){
  uint32_t *out=cm_pretri+64*t;for(uint32_t k=0;k<64;k++)out[k]=0;
  const uint32_t *v[3];for(uint32_t j=0;j<3;j++)v[j]=cm_projected+8*cm_indices[3*t+j];
  if(!(v[0][7] && v[1][7] && v[2][7]))continue;
  int64_t area=cm_edge(v[0],v[1],(int32_t)v[2][0],(int32_t)v[2][1]);if(!area)continue;
  int64_t sign=area<0?-1:1;area*=sign;
  int64_t edges[3][3];
  for(uint32_t j=0;j<3;j++){
   const uint32_t *aa=v[(j+1)%3],*bb=v[(j+2)%3];
   int64_t ax=(int32_t)aa[0],ay=(int32_t)aa[1],bx=(int32_t)bb[0],by=(int32_t)bb[1];
   edges[j][0]=(ay-by)*sign;edges[j][1]=(bx-ax)*sign;edges[j][2]=(by*ax-bx*ay)*sign;
   for(uint32_t k=0;k<3;k++){if(edges[j][k]<INT32_MIN || edges[j][k]>INT32_MAX)return fail("P3_EDGE_RANGE",t,0,k);out[4*j+k]=(uint32_t)edges[j][k];}
  }
  out[12]=(uint32_t)area;out[13]=t;out[14]=v[0][0];out[15]=v[0][1];out[60]=1;
  out[36]=cm_material_color(t);
  out[37]=cm_asset->material_mix?cm_asset->material_mix[t]:0u;
  for(uint32_t attr=0;attr<5;attr++){
   int64_t pa=0,pb=0,anchor=area*v[0][2+attr];
   for(uint32_t j=0;j<3;j++){pa+=edges[j][0]*v[j][2+attr];pb+=edges[j][1]*v[j][2+attr];}
   if(pa<INT32_MIN || pa>INT32_MAX || pb<INT32_MIN || pb>INT32_MAX || anchor>INT32_MAX)return fail("P3_PLANE_RANGE",t,0,attr);
   out[16+4*attr]=(uint32_t)pa;out[17+4*attr]=(uint32_t)pb;out[18+4*attr]=(uint32_t)anchor;
  }
 }
 return cm_upload_used(8,64u*CM_FACES);
}
static uint32_t cm_bilinear(uint32_t uq,uint32_t vq){
 uint32_t limit=(CM_TEX_SIZE-1u)*16u;
 if(uq>limit)uq=limit;
 if(vq>limit)vq=limit;
 uint32_t u=uq>>4,v=vq>>4,fu=uq&15,fv=vq&15,u1=u<CM_TEX_SIZE-1u?u+1:CM_TEX_SIZE-1u,v1=v<CM_TEX_SIZE-1u?v+1:CM_TEX_SIZE-1u;
 uint32_t tap[4]={cm_texture[CM_TEX_SIZE*v+u],cm_texture[CM_TEX_SIZE*v+u1],cm_texture[CM_TEX_SIZE*v1+u],cm_texture[CM_TEX_SIZE*v1+u1]};
 uint32_t w[4]={(16-fu)*(16-fv),fu*(16-fv),(16-fu)*fv,fu*fv},result=0xff000000u;
 for(uint32_t shift=0;shift<24;shift+=8){uint32_t sum=128;for(uint32_t k=0;k<4;k++)sum+=((tap[k]>>shift)&255)*w[k];result|=(sum>>8)<<shift;}
 return result;
}
static void cm_golden(void){
 for(uint32_t p=0;p<CM_PIXELS;p++){cm_colors[p]=CM_WIDTH==512u?cm_background_high[p]:cm_background[p];cm_depths[p]=UINT32_MAX;cm_owners[p]=CM_FACES;}
 /* Independent of the tile directory: enumerate each whole-triangle bbox. */
 for(uint32_t t=0;t<CM_FACES;t++){
  int32_t b[4];
 if(!cm_box(t,b))continue;
  const uint32_t *v[3];for(uint32_t j=0;j<3;j++)v[j]=cm_projected+8u*cm_indices[3u*t+j];
  int64_t area=cm_edge(v[0],v[1],(int32_t)v[2][0],(int32_t)v[2][1]);
 if(!area)continue;
 int negative=area<0;
 if(negative)area=-area;
  for(int32_t y=b[2];y<=b[3];y++)for(int32_t x=b[0];x<=b[1];x++){
   int64_t px=4*x+2,py=4*y+2,w[3]={cm_edge(v[1],v[2],px,py),cm_edge(v[2],v[0],px,py),cm_edge(v[0],v[1],px,py)};
   if(negative)for(uint32_t j=0;j<3;j++)w[j]=-w[j];
 if(w[0]<0 || w[1]<0 || w[2]<0)continue;
   int64_t den=0;for(uint32_t j=0;j<3;j++)den+=w[j]*v[j][2];uint32_t z=4096u-(uint32_t)(den*16/area),pixel=(uint32_t)(CM_WIDTH*y+x);
   if(z>cm_depths[pixel] || (z==cm_depths[pixel] && t>=cm_owners[pixel]))continue;
   uint32_t uv[2];for(uint32_t k=0;k<2;k++){int64_t n=0;for(uint32_t j=0;j<3;j++)n+=w[j]*v[j][3u+k];uv[k]=(uint32_t)(n*(CM_TEX_SIZE/16u)/den);
 if(uv[k]>(CM_TEX_SIZE-1u)*16u)uv[k]=(CM_TEX_SIZE-1u)*16u;}
   uint32_t texel=cm_bilinear(uv[0],uv[1]),light[2];
   for(uint32_t k=0;k<2;k++){int64_t n=0;for(uint32_t j=0;j<3;j++)n+=w[j]*v[j][5+k];light[k]=(uint32_t)(n/den);}
   uint32_t shaded=0xff000000u;for(uint32_t shift=0;shift<24;shift+=8){
    uint32_t sampled=(texel>>shift)&255u;
    if(cm_asset->material_rgb){uint32_t mix=cm_asset->material_mix[t],paint=(cm_material_color(t)>>shift)&255u;sampled=(sampled*(256u-mix)+paint*mix+128u)>>8;}
    uint32_t ch=((sampled*light[0]+128)>>8)+light[1];if(ch>255)ch=255;shaded|=ch<<shift;
   }
   cm_colors[pixel]=shaded;cm_depths[pixel]=z;cm_owners[pixel]=t;
  }
 }
}
static int cm_transform(uint32_t frame){
 uint32_t args[15]={CM_RAW,CM_PROJECTED,CM_ACTIVE_SLOTS};if(cm_validate)cm_project_gold(frame);
 if(cm_validate)cm_init_buffer(4,1);
 for(uint32_t i=0;i<12;i++)args[3+i]=cm_matrices[12u*frame+i];
 if(v3_launch(CM_WIDTH==512u?cm_transform_high_kernel:cm_transform_kernel,CM_WIDTH==512u?sizeof(cm_transform_high_kernel)/4u:sizeof(cm_transform_kernel)/4u,args,15) || cm_check_buffers())return -1;
 host_log("COMPLEX_TRANSFORM_PASS frame=%u vertices=%u slots=16064 active_slots=%u words=%u guards=320 elapsed=%u jobs=%u errors=%08x\r\n",frame,(uint32_t)CM_REAL_VERTICES,(uint32_t)CM_ACTIVE_SLOTS,(uint32_t)(8u*CM_ACTIVE_SLOTS),rd(GPU_ELAPSED),rd(GPU_JOBS),rd(GPU_ERRORS));
 if(v3_ack())return -1;
 if(!cm_validate)for(uint32_t i=0;i<8u*CM_ACTIVE_SLOTS;i++)cm_projected[i]=host_ps_read(CM_PROJECTED+4*i);
 return cm_build_pretri() || cm_build_tiles();
}
static int cm_render(uint32_t frame,uint32_t reverse,const char *texture,uint32_t *crc){
 uint32_t args[10]={FB64_BASE,V3_DEPTH,CM_PRETRI,CM_DIR,V3_TEX,CM_REFS,UINT32_C(0xffece4dc),reverse,CM_FACES,CM_BG};
 if(cm_validate){cm_golden();cm_init_buffer(0,1);cm_init_buffer(1,1);}
 if(v3_launch(CM_WIDTH==512u?cm_raster_high_kernel:cm_raster_kernel,CM_WIDTH==512u?sizeof(cm_raster_high_kernel)/4u:sizeof(cm_raster_kernel)/4u,args,10) || cm_check_buffers())return -1;
 uint32_t covered=0;if(cm_validate)for(uint32_t p=0;p<CM_PIXELS;p++){
  uint32_t c=host_ps_read(FB64_BASE+4u*p),z=host_ps_read(V3_DEPTH+4u*p);
  if(c!=cm_colors[p])return fail("COMPLEX_PIXEL",p,cm_colors[p],c);
  if(z!=cm_depths[p])return fail("COMPLEX_DEPTH",p,cm_depths[p],z);
  covered+=z!=UINT32_MAX;
 }
 for(uint32_t p=0;p<CM_PIXELS;p++){cm_colors[p]=host_ps_read(FB64_BASE+4*p);cm_depths[p]=host_ps_read(V3_DEPTH+4*p);}
 *crc=cm_crc(cm_colors,CM_PIXELS);cm_last_depth_crc=cm_crc(cm_depths,CM_PIXELS);
 host_log("COMPLEX_RENDER_PASS frame=%u reverse=%u texture=%s pixels=%u depth_words=%u guards=320 covered=%u refs=%u crc32=%08x elapsed=%u jobs=%u errors=%08x\r\n",frame,reverse,texture,(uint32_t)CM_PIXELS,(uint32_t)CM_PIXELS,covered,cm_active_refs,*crc,rd(GPU_ELAPSED),rd(GPU_JOBS),rd(GPU_ERRORS));
 return v3_ack();
}
int host_run_complex_gate(void){
 cm_configure();
 if(!cm_theme_ready){cm_theme_colors(cm_qinghua_texture,cm_theme_palette);cm_theme_ready=1;}
 uint32_t before=rd(GPU_JOBS),first_crc=0,crc=0;uint64_t begin=render_clock_ticks();cm_validate=1;
 if(rd(GPU_STATUS)!=0 || rd(GPU_MEMORY_READY)!=1)return fail("P3_PREPARE_STATE",0,0,rd(GPU_STATUS));
 const uint32_t *source=cm_saved_nn_texture;
 uint32_t live_base=CM_TEX_SIZE==2048u?0x16400000u:0x16000000u;
 for(uint32_t i=0;i<CM_TEX_SIZE*CM_TEX_SIZE;i++)cm_texture[i]=cm_committed?host_ps_read(live_base+4u*i):source[i];
 for(uint32_t b=0;b<10;b++)cm_init_buffer(b,b<2 || b==4);
 host_log("P3_PREFLIGHT_BEGIN vertices=%u triangles=%u texture_width=%u width=%u epoch=%u poses=%u bilinear=1 winner_only=1\r\n",(uint32_t)CM_REAL_VERTICES,(uint32_t)CM_FACES,(uint32_t)CM_TEX_SIZE,(uint32_t)CM_WIDTH,cm_texture_epoch,(uint32_t)(CM_WIDTH==512u?8u:4u));
 uint32_t views=CM_WIDTH==512u?8u:4u;
 for(uint32_t view=0;view<views;view++){
  uint32_t frame=CM_WIDTH==512u?cm_warrior_poses[view]:25u*view;
  if(cm_transform(frame) || cm_render(frame,0,"user_qinghua512",&crc))return -1;
  if(!frame){first_crc=crc;if(cm_render(frame,1,"user_qinghua512",&crc))return -1;
   if(crc!=first_crc)return fail("P3_ORDER",0,first_crc,crc);}
 }
 if(rd(GPU_JOBS)!=before+2u*views+1u)return fail("P3_JOB_TOTAL",0,before+2u*views+1u,rd(GPU_JOBS));
 if(!render_clock_frequency() || render_clock_ticks()<=begin)return fail("P3_CLOCK",0,1,0);
 host_log("P3_PREFLIGHT_PASS transforms=%u renders=%u color_words=%u depth_words=%u guards_per_check=320 jobs=%u errors=%08x NN=0\r\n",views,views+1u,(uint32_t)((views+1u)*CM_PIXELS),(uint32_t)((views+1u)*CM_PIXELS),rd(GPU_JOBS),rd(GPU_ERRORS));
 return 0;
}
int host_main(void){int result=baseline_host_main();return result?result:host_run_complex_gate();}


uint32_t host_display_native_width(void){return CM_WIDTH;}
uint32_t host_display_texture_width(void){return CM_TEX_SIZE;}
uint32_t host_display_texture_epoch(void){return cm_texture_epoch;}
static const uint32_t cm_live_bases[4]={0x16000000u,0x16400000u,0x17800000u,0x18000000u};
static const uint32_t cm_live_bytes[4]={1048576u,16777216u,1048576u,16777216u};
void host_display_texture_storage_init(void){
 for(uint32_t b=0;b<4;b++)for(uint32_t side=0;side<2;side++){
  uint32_t address=side?cm_live_bases[b]+cm_live_bytes[b]:cm_live_bases[b]-64u;
  for(uint32_t i=0;i<16;i++)host_ps_write(address+4u*i,v3_guard(b+10u,side,i));
  host_flush(address,64u);
 }
 host_barrier();
}
int host_display_texture_storage_check(void){
 for(uint32_t b=0;b<4;b++)for(uint32_t side=0;side<2;side++){
  uint32_t address=side?cm_live_bases[b]+cm_live_bytes[b]:cm_live_bases[b]-64u;
  host_invalidate(address,64u);host_barrier();
  for(uint32_t i=0;i<16;i++)if(host_ps_read(address+4u*i)!=v3_guard(b+10u,side,i))return fail("TEXTURE_STORAGE_GUARD",b*32u+side*16u+i,v3_guard(b+10u,side,i),host_ps_read(address+4u*i));
 }
 return 0;
}
int host_display_texture_ready_check(uint32_t small_crc,uint32_t large_crc){
 uint32_t expected[2]={small_crc,large_crc};
 for(uint32_t b=0;b<2;b++){
  uint32_t c=UINT32_MAX;
  for(uint32_t i=0;i<cm_live_bytes[b+2u]/4u;i++){
   uint32_t word=host_ps_read(cm_live_bases[b+2u]+4u*i);
   for(uint32_t j=0;j<4;j++){c=(c>>8)^hr_crc_table[(c^word)&255u];word>>=8;}
  }
  if(~c!=expected[b])return fail("TEXTURE_READY_CRC",b,expected[b],~c);
 }
 return host_display_texture_storage_check();
}
int host_display_texture_commit(const uint32_t *small,const uint32_t *large,uint32_t epoch){
 if(!small||!large||!epoch||rd(GPU_STATUS)||rd(GPU_ERRORS)||rd(GPU_PENDING_WRITES)||rd(GPU_PENDING_READS))return fail("TEXTURE_COMMIT_BUSY",0,0,rd(GPU_STATUS));
 if(host_display_texture_storage_check())return -1;
 for(uint32_t i=0;i<262144u;i++)host_ps_write(0x16000000u+4u*i,small[i]);
 for(uint32_t i=0;i<4194304u;i++)host_ps_write(0x16400000u+4u*i,large[i]);
 host_flush(0x16000000u,1048576u);host_flush(0x16400000u,16777216u);host_barrier();
 const uint32_t *active=CM_TEX_SIZE==2048u?large:small;
 for(uint32_t i=0;i<CM_TEX_SIZE*CM_TEX_SIZE;i++)cm_texture[i]=active[i];
 cm_theme_colors(small,cm_theme_palette);cm_theme_ready=1;
 cm_committed=1;cm_texture_epoch=epoch;cm_init_buffer(2,0);
 return cm_check_buffers() || host_display_texture_storage_check();
}
