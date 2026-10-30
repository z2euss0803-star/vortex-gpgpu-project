#include "display_fixture.hpp"
#include <limits>
static PublishedResult source(AppKind app,const char*kind,vxnn::Shape shape,const std::vector<uint8_t>&raw){
 auto p=producer();p.lease.app=app;p.lease.artifacts[0].layout=kind;p.lease.artifacts[0].shape=std::move(shape);p.lease.artifacts[0].bytes=raw.size();p.lease.artifacts[0].sha=sum(raw);return p;
}
static void rgb_at(const DisplayCanvas&c,uint32_t x,uint32_t y,uint8_t r,uint8_t g,uint8_t b){size_t i=(size_t(y)*c.width+x)*3;need(c.rgb[i]==r&&c.rgb[i+1]==g&&c.rgb[i+2]==b,"hand-derived page pixel");}
int main(){
 for(uint32_t n:{256u,512u}){
  std::vector<uint8_t>raw(size_t(n)*n*4,255);for(size_t i=0;i<raw.size();i+=4){raw[i]=17;raw[i+1]=31;raw[i+2]=49;}
  auto p=source(AppKind::P8,"RGBA8888",{n,n,4},raw);auto l=get(raster_page_layout(p.lease,0,1280,720),"P8 native page layout required");
  need(l.viewport_width==512&&l.viewport_height==512&&l.viewport_x==384&&l.viewport_y==104,"P8 256 doubled and 512 native centered");
  auto page=get(compose_raster_page(p,0,{raw.data(),raw.size()},l),"native P8 raster composition");rgb_at(*page,384,104,17,31,49);rgb_at(*page,895,615,17,31,49);rgb_at(*page,383,104,0,0,0);need(page->source_artifact==p.lease.artifacts[0].sha&&page->pixel_sha==sum(page->rgb),"page source and pixel digests");
  auto changed=raw;changed[0]^=1;need(!compose_raster_page(p,0,{changed.data(),changed.size()},l),"changed raw artifact refused");
  need(!raster_page_layout(p.lease,0,400,400),"native P8 page cannot silently downscale");
 }
 std::vector<uint8_t>raw(1280*720*4,255);for(size_t i=0;i<raw.size();i+=4){raw[i]=11;raw[i+1]=22;raw[i+2]=33;}
 auto p=source(AppKind::IMAGE,"RGBA8888",{720,1280,4},raw);auto l=get(raster_page_layout(p.lease,0,1920,1080),"image native canvas layout");need(l.viewport_x==320&&l.viewport_y==180&&l.viewport_width==1280&&l.viewport_height==720,"image stays native720 on1080p");auto page=get(compose_raster_page(p,0,{raw.data(),raw.size()},l),"image native page");rgb_at(*page,320,180,11,22,33);rgb_at(*page,319,180,0,0,0);
 std::vector<float>planes={-1,255,999,1.9f, 2,3,4,5, 6,7,8,9};std::vector<uint8_t>tensor(planes.size()*4);std::memcpy(tensor.data(),planes.data(),tensor.size());auto m=source(AppKind::NN,"CHW_F32",{3,2,2},tensor);auto ml=get(raster_page_layout(m.lease,0,4,2),"Mosaic CHW page layout");need(ml.viewport_width==2&&ml.viewport_height==2&&ml.viewport_x==1,"Mosaic aspect preserved");auto mp=get(compose_raster_page(m,0,{tensor.data(),tensor.size()},ml),"Mosaic original clamp/truncate");rgb_at(*mp,1,0,0,2,6);rgb_at(*mp,2,0,255,3,7);rgb_at(*mp,1,1,255,4,8);rgb_at(*mp,2,1,1,5,9);
 planes[0]=std::numeric_limits<float>::quiet_NaN();std::memcpy(tensor.data(),planes.data(),tensor.size());m.lease.artifacts[0].sha=sum(tensor);need(!compose_raster_page(m,0,{tensor.data(),tensor.size()},ml),"nonfinite Mosaic output refused");
 puts("U18_NATIVE_P8_IMAGE_MOSAIC_PAGE_COMPOSITION_PASS_HOST_NOT_DP gpu_raw_cycles=null");return 0;
}
