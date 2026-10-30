void cm_project_gold(uint32_t frame){
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
int64_t cm_edge(const uint32_t *a,const uint32_t *b,int64_t x,int64_t y){
 return ((int64_t)(int32_t)b[0]-(int32_t)a[0])*(y-(int32_t)a[1])-((int64_t)(int32_t)b[1]-(int32_t)a[1])*(x-(int32_t)a[0]);
}
int cm_box(uint32_t tid,int32_t box[4]){
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
int cm_tile_intersects(uint32_t tid,uint32_t tx,uint32_t ty){
 const uint32_t *p=cm_pretri+64*tid;if(!p[12])return 0;
 for(uint32_t j=0;j<3;j++){
  int32_t a=(int32_t)p[4*j],b=(int32_t)p[4*j+1],c=(int32_t)p[4*j+2];
  int32_t x=(int32_t)(16*tx)+(a>=0?14:2),y=(int32_t)(16*ty)+(b>=0?14:2);
  if((int64_t)a*x+(int64_t)b*y+c<0)return 0;
 }
 return 1;
}
int cm_build_tiles(void){
 uint32_t counts[16384];for(uint32_t tile=0;tile<CM_TILES;tile++)counts[tile]=0;cm_active_refs=0;
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
int cm_build_pretri(void){
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
uint32_t cm_bilinear(uint32_t uq,uint32_t vq){
 uint32_t limit=(CM_TEX_SIZE-1u)*16u;
 if(uq>limit)uq=limit;
 if(vq>limit)vq=limit;
 uint32_t u=uq>>4,v=vq>>4,fu=uq&15,fv=vq&15,u1=u<CM_TEX_SIZE-1u?u+1:CM_TEX_SIZE-1u,v1=v<CM_TEX_SIZE-1u?v+1:CM_TEX_SIZE-1u;
 uint32_t tap[4]={cm_texture[CM_TEX_SIZE*v+u],cm_texture[CM_TEX_SIZE*v+u1],cm_texture[CM_TEX_SIZE*v1+u],cm_texture[CM_TEX_SIZE*v1+u1]};
 uint32_t w[4]={(16-fu)*(16-fv),fu*(16-fv),(16-fu)*fv,fu*fv},result=0xff000000u;
 for(uint32_t shift=0;shift<24;shift+=8){uint32_t sum=128;for(uint32_t k=0;k<4;k++)sum+=((tap[k]>>shift)&255)*w[k];result|=(sum>>8)<<shift;}
 return result;
}
void cm_golden(void){
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
