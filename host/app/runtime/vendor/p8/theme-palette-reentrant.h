#ifndef THEME_PALETTE_H
#define THEME_PALETTE_H
#include <stdint.h>
#define CM_THEME_API 1
/* Fixed 512 LOD, 32x32 sample centers; only called at default init or commit. */
static uint32_t cm_pack_rgb(uint32_t r,uint32_t g,uint32_t b){return 0xff000000u|r|(g<<8)|(b<<16);}
static uint32_t cm_theme_bin(uint32_t r,uint32_t g,uint32_t b){return ((r>>5)<<6)|((g>>5)<<3)|(b>>5);}
static int cm_theme_chromatic(uint32_t r,uint32_t g,uint32_t b){
 uint32_t mx=r>g?r:g;mx=mx>b?mx:b;uint32_t mn=r<g?r:g;mn=mn<b?mn:b;
 return mx>=48u && mx-mn>=24u;
}
static void cm_theme_colors(const uint32_t *small,uint32_t colors[10]){
 uint16_t bins[512];for(uint32_t i=0;i<512;i++)bins[i]=0;
 for(uint32_t y=8;y<512;y+=16)for(uint32_t x=8;x<512;x+=16){
  uint32_t t=small[512*y+x],r=t&255u,g=(t>>8)&255u,b=(t>>16)&255u;
  if(cm_theme_chromatic(r,g,b))bins[cm_theme_bin(r,g,b)]++;
 }
 uint32_t winner=0,count=0;for(uint32_t i=0;i<512;i++)if(bins[i]>count){winner=i;count=bins[i];}
 uint32_t sums[3]={0,0,0},n=0;
 for(uint32_t y=8;y<512;y+=16)for(uint32_t x=8;x<512;x+=16){
  uint32_t t=small[512*y+x],r=t&255u,g=(t>>8)&255u,b=(t>>16)&255u;
  int accept;
  if(count)accept=cm_theme_chromatic(r,g,b)&&cm_theme_bin(r,g,b)==winner;
  else{uint32_t mx=r>g?r:g;mx=mx>b?mx:b;uint32_t mn=r<g?r:g;mn=mn<b?mn:b;accept=mx>=24u&&mn<=240u;}
  if(accept){sums[0]+=r;sums[1]+=g;sums[2]+=b;n++;}
 }
 uint32_t primary[3];for(uint32_t k=0;k<3;k++)primary[k]=n?(sums[k]+n/2u)/n:192u;
 const uint32_t ivory[3]={228,220,207};
 for(uint32_t region=0;region<10;region++){
  uint32_t rgb[3];
  for(uint32_t k=0;k<3;k++){
   uint32_t v=primary[k];
   if(region==3)rgb[k]=(2u*v+129u)/3u;
   else if(region==1||region==5)rgb[k]=(3u*v+146u)/4u;
   else if(region==7||region==8)rgb[k]=(v*(region==7?6u:5u)+8u)/16u+12u;
   else rgb[k]=(v+3u*ivory[k]+2u)/4u;
  }
  colors[region]=cm_pack_rgb(rgb[0],rgb[1],rgb[2]);
 }
 /* Independent integer equivalent of the low-saturation face/hand glaze. */
 uint32_t luma=(77u*primary[0]+150u*primary[1]+29u*primary[2]+128u)/256u;
 int32_t neutral=176+(int32_t)((24u*luma+127u)/255u),tint[3];
 const int32_t bias[3]={6,2,-4};
 for(uint32_t k=0;k<3;k++){
  int32_t delta=3*((int32_t)primary[k]-(int32_t)luma);
  int32_t accent=delta>=0?(delta+4)/8:-((-delta+4)/8);
  tint[k]=neutral+bias[k]+accent;if(tint[k]<144)tint[k]=144;if(tint[k]>220)tint[k]=220;
 }
 colors[4]=cm_pack_rgb((uint32_t)tint[0],(uint32_t)tint[1],(uint32_t)tint[2]);
 colors[5]=colors[4];
 colors[6]=cm_pack_rgb(51,43,36);colors[9]=cm_pack_rgb(150,143,133);
}
#endif
