#include "internal.hpp"
#include <vxl/hunet.h>
#include <cmath>
#include <cstring>
#include <array>
#include <tuple>
namespace vxunified {
namespace display_detail {
struct CanvasCompositionAuthority {
 static void invalidate(DisplayCanvas&c)noexcept{c.seal_={};}
 static void seal(DisplayCanvas&c)noexcept{c.seal_={&c,c.rgb.data(),c.rgb.size(),c.width,c.height,c.pixel_sha,c.source_artifact};}
};
}
namespace {
struct Raster {uint32_t w=0,h=0,channels=0;bool fp32=false;};
Result<Raster> describe(const ResultLease&l,size_t index){
 if(index>=l.artifacts.size())return Result<Raster>::failure({EINVAL,"DISPLAY","raster artifact index"});
 const auto&a=l.artifacts[index];Raster r;
 if(a.shape.size()!=3)return Result<Raster>::failure({EOPNOTSUPP,"DISPLAY","non-raster result requires its dedicated page"});
 if(a.layout=="RGBA8888"||a.layout=="RGB888"){
  r.channels=a.layout=="RGBA8888"?4:3;
  if(a.shape[2]!=r.channels||a.shape[0]>1080||a.shape[1]>1920)return Result<Raster>::failure({EINVAL,"DISPLAY","bounded HWC raster required"});
  r.h=uint32_t(a.shape[0]);r.w=uint32_t(a.shape[1]);
 }else if(a.layout=="CHW_F32"){
  if(a.shape[0]!=3||a.shape[1]>1080||a.shape[2]>1920)return Result<Raster>::failure({EOPNOTSUPP,"DISPLAY","Mosaic CHW three-channel raster required"});
  r.h=uint32_t(a.shape[1]);r.w=uint32_t(a.shape[2]);r.channels=3;r.fp32=true;
 }else return Result<Raster>::failure({EOPNOTSUPP,"DISPLAY","unsupported raster layout"});
 if(!r.w||!r.h||a.bytes!=uint64_t(r.w)*r.h*r.channels*(r.fp32?4:1))return Result<Raster>::failure({EINVAL,"DISPLAY","exact raster extent required"});
 return Result<Raster>::success(r);
}
float load_float(const uint8_t*p){uint32_t bits=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);float f;static_assert(sizeof(f)==4,"FP32 required");std::memcpy(&f,&bits,4);return f;}
bool same_layout(const DisplayLayout&a,const DisplayLayout&b){return a.canvas_width==b.canvas_width&&a.canvas_height==b.canvas_height&&a.source_width==b.source_width&&a.source_height==b.source_height&&a.viewport_x==b.viewport_x&&a.viewport_y==b.viewport_y&&a.viewport_width==b.viewport_width&&a.viewport_height==b.viewport_height&&a.format==b.format;}
bool hex_text(const char*s,size_t n){for(size_t i=0;i<n;i++){if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;}return s[n]==0;}
}
Result<std::shared_ptr<const DisplayCanvas>> compose_hunet_page(const PublishedResult&p,size_t index,vxnn::Bytes raw,const demo_hunet_result&view,const DisplayLayout&l){
 auto fail=[](int n,const char*s){return Result<std::shared_ptr<const DisplayCanvas>>::failure({n,"DISPLAY_HUNET",s});};
 try{
  auto valid=validate_published_result(p);if(!valid)return Result<std::shared_ptr<const DisplayCanvas>>::failure(*valid.error);
  if(p.lease.app!=AppKind::NN||index>=p.lease.artifacts.size())return fail(EINVAL,"retained HUNet artifact required");
  const auto&a=p.lease.artifacts[index];
  if(a.layout!="CHW_F32"||a.shape!=vxnn::Shape{8105,1,1}||a.bytes!=32420||raw.size!=a.bytes||!raw.data)return fail(EINVAL,"exact HUNet logits contract required");
  Digest actual{};vxl_hunet_hash(raw.data,raw.size,actual.data());if(actual!=a.sha)return fail(EBADMSG,"retained HUNet logits changed");
  if(!view.completed||!view.source_frame||demo_frame_check(view.source_frame,0)||!hex_text(view.source_result_sha,64)||!hex_text(view.logits_sha,64)||!hex_text(view.classes_sha,64)||!hex_text(view.glyph_sha,64)||!hex_text(view.run_id,32)||!hex_text(view.source_frame->sha256,64))return fail(EINVAL,"complete original HUNet view metadata required");
  char digest[65]{};demo_sha_hex(raw.data,raw.size,digest);if(std::memcmp(digest,view.logits_sha,65))return fail(ESTALE,"view logits identity differs");
  demo_sha_hex(p.raw_legacy_json.data(),p.raw_legacy_json.size(),digest);if(p.raw_legacy_json.empty()||std::memcmp(digest,view.source_result_sha,65))return fail(ESTALE,"original result evidence differs");
  const auto&input=*view.source_frame;demo_sha_hex(input.pixels,input.bytes,digest);if(std::memcmp(digest,input.sha256,65))return fail(ESTALE,"original source image changed");
  if(view.bbox.image_width!=input.width||view.bbox.image_height!=input.height)return fail(EINVAL,"ROI image dimensions differ");
  const auto numeric=p.numeric==vxnn::NumericClass::PASS?DEMO_NUMERIC_PASS:p.numeric==vxnn::NumericClass::KNOWN_NUMERIC_DEVIATION?DEMO_NUMERIC_KNOWN_DEVIATION:p.numeric==vxnn::NumericClass::NEW_MISMATCH?DEMO_NUMERIC_FAIL:DEMO_NUMERIC_UNEVALUATED;
  if(view.numeric_class!=numeric)return fail(EPROTO,"display numeric class differs from original producer");
  if(numeric==DEMO_NUMERIC_KNOWN_DEVIATION&&(!view.history_byte_equal||!view.ort_evaluated||!view.r2_evaluated||(view.ort_pass&&view.r2_pass)))return fail(EPROTO,"known deviation must retain original failed numeric evidence");
  if(view.view_scope!=DEMO_VIEW_OFFLINE&&view.view_scope!=DEMO_VIEW_HISTORICAL&&view.view_scope!=DEMO_VIEW_LIVE)return fail(EINVAL,"unknown HUNet view scope");
  if(view.view_scope==DEMO_VIEW_LIVE&&p.lease.scope!=vxnn::EvidenceScope::REAL_BOARD)return fail(EPERM,"offline HUNet result cannot claim LIVE");
  std::vector<float>logits(DEMO_HUNET_CLASSES);std::array<uint32_t,DEMO_HUNET_TOPK>top;top.fill(UINT32_MAX);
  for(uint32_t id=0;id<DEMO_HUNET_CLASSES;id++){
   logits[id]=load_float(raw.data+size_t(id)*4);if(!std::isfinite(logits[id]))return fail(EDOM,"nonfinite original HUNet logits");
   for(size_t rank=0;rank<top.size();rank++){
    uint32_t old=top[rank];if(old==UINT32_MAX||logits[id]>logits[old]||(logits[id]==logits[old]&&id<old)){
     for(size_t j=top.size()-1;j>rank;j--){top[j]=top[j-1];}
     top[rank]=id;break;
    }
   }
  }
  for(size_t i=0;i<top.size();i++){
   if(view.top[i].class_id!=top[i]||std::memcmp(&view.top[i].score,&logits[top[i]],4)||view.glyphs[i].class_id!=top[i])return fail(ESTALE,"original decoded top5/glyph identity differs from logits");
  }
  if(l.canvas_width<640||l.canvas_height<480||l.canvas_width>1920||l.canvas_height>1080||l.source_width!=input.width||l.source_height!=input.height||l.viewport_x||l.viewport_y||l.viewport_width!=l.canvas_width*3/4||l.viewport_height!=l.canvas_height||l.format!=PixelFormat::RGBA8888)return fail(EINVAL,"original HUNet panel layout required");
  auto out=std::make_shared<DisplayCanvas>();out->width=l.canvas_width;out->height=l.canvas_height;out->source_artifact=a.sha;out->rgb.resize(size_t(out->width)*out->height*3);
  demo_frame frame{};frame.width=out->width;frame.height=out->height;frame.stride=frame.width*3;frame.bytes=out->rgb.size();frame.pixels=out->rgb.data();
  int rc=demo_frame_from_hunet(&view,&frame);if(rc)return fail(-rc,"original HUNet compositor refused metadata");
  vxl_hunet_hash(out->rgb.data(),out->rgb.size(),out->pixel_sha.data());display_detail::CanvasCompositionAuthority::seal(*out);return Result<std::shared_ptr<const DisplayCanvas>>::success(std::move(out));
 }catch(const std::bad_alloc&){return fail(ENOMEM,"HUNet page allocation");}
}
Result<DisplayLayout> raster_page_layout(const ResultLease&lease,size_t index,uint32_t w,uint32_t h){
 auto source=describe(lease,index);if(!source)return Result<DisplayLayout>::failure(*source.error);
 if(!w||!h||w>1920||h>1080)return Result<DisplayLayout>::failure({ERANGE,"DISPLAY","bounded native canvas required"});
 auto r=*source.value;uint32_t fitw=0,fith=0;
 if(lease.app==AppKind::P8){
  if(r.fp32||r.channels!=4||r.w!=r.h||(r.w!=256&&r.w!=512))return Result<DisplayLayout>::failure({EINVAL,"DISPLAY","original P8 native raster required"});
  fitw=fith=512;
 }else if(lease.app==AppKind::IMAGE){
  if(r.fp32||r.w!=1280||r.h!=720)return Result<DisplayLayout>::failure({EINVAL,"DISPLAY","image studio native720 required"});
  fitw=1280;fith=720;
 }else if(lease.app==AppKind::NN&&r.fp32){
  fitw=w;fith=uint32_t(uint64_t(r.h)*w/r.w);
  if(fith>h){fith=h;fitw=uint32_t(uint64_t(r.w)*h/r.h);}
  fitw=std::max(1u,fitw);fith=std::max(1u,fith);
 }else return Result<DisplayLayout>::failure({EOPNOTSUPP,"DISPLAY","no admitted raster page for application"});
 if(fitw>w||fith>h)return Result<DisplayLayout>::failure({ENOSPC,"DISPLAY","native page cannot fit without changing its contract"});
 DisplayLayout out{w,h,r.w,r.h,(w-fitw)/2,(h-fith)/2,fitw,fith,PixelFormat::RGBA8888};return Result<DisplayLayout>::success(out);
}
namespace display_detail {
struct RasterCacheState {
 mutable std::mutex mutex;std::shared_ptr<DisplayCanvas>canvas;
 std::array<uint32_t,1920>x{};std::array<uint32_t,1080>row{};
 std::tuple<uint32_t,uint32_t,uint32_t,uint32_t>key{};bool mapped=false;
 RasterComposeStats counts;
};
static thread_local RasterComposeCache*selected_cache=nullptr;
RasterCacheScope::RasterCacheScope(RasterComposeCache*cache):previous(selected_cache){selected_cache=cache;}
RasterCacheScope::~RasterCacheScope(){selected_cache=previous;}
RasterComposeCache*active_raster_cache(){return selected_cache;}
}
RasterComposeCache::RasterComposeCache():state_(std::make_shared<display_detail::RasterCacheState>()){}
RasterComposeStats RasterComposeCache::stats()const{std::lock_guard<std::mutex>lock(state_->mutex);auto out=state_->counts;out.retained_canvases=state_->canvas?1:0;out.retained_bytes=sizeof(state_->x)+sizeof(state_->row)+(state_->canvas?state_->canvas->rgb.capacity():0);return out;}
static Result<std::shared_ptr<const DisplayCanvas>> compose_raster_impl(const PublishedResult&p,size_t index,vxnn::Bytes raw,const DisplayLayout&layout,display_detail::RasterCacheState*cache){
 try{
  auto valid=validate_published_result(p);if(!valid)return Result<std::shared_ptr<const DisplayCanvas>>::failure(*valid.error);
  auto source=describe(p.lease,index);if(!source)return Result<std::shared_ptr<const DisplayCanvas>>::failure(*source.error);
  auto expected=raster_page_layout(p.lease,index,layout.canvas_width,layout.canvas_height);if(!expected)return Result<std::shared_ptr<const DisplayCanvas>>::failure(*expected.error);
  if(!same_layout(layout,*expected.value))return Result<std::shared_ptr<const DisplayCanvas>>::failure({EINVAL,"DISPLAY","page layout differs from original native contract"});
  const auto&a=p.lease.artifacts[index];Digest actual{};
  if(!raw.data||raw.size!=a.bytes)return Result<std::shared_ptr<const DisplayCanvas>>::failure({EBADMSG,"DISPLAY","retained raster extent differs"});
  vxl_hunet_hash(raw.data,raw.size,actual.data());if(actual!=a.sha)return Result<std::shared_ptr<const DisplayCanvas>>::failure({EBADMSG,"DISPLAY","retained raster digest differs"});
  auto r=*source.value;
  if(r.fp32){for(size_t i=0;i<raw.size;i+=4){if(!std::isfinite(load_float(raw.data+i)))return Result<std::shared_ptr<const DisplayCanvas>>::failure({EDOM,"DISPLAY","nonfinite original Mosaic output"});}}
  else if(r.channels==4){for(size_t i=3;i<raw.size;i+=4){if(raw.data[i]!=255)return Result<std::shared_ptr<const DisplayCanvas>>::failure({EBADMSG,"DISPLAY","opaque original RGBA required"});}}
  std::unique_lock<std::mutex>lock;std::shared_ptr<DisplayCanvas>out;
  if(cache){lock=std::unique_lock<std::mutex>(cache->mutex);if(cache->canvas&&cache->canvas.use_count()==1){out=cache->canvas;++cache->counts.buffer_reuses;}else{out=std::make_shared<DisplayCanvas>();++cache->counts.buffer_allocations;if(!cache->canvas)cache->canvas=out;}}
  else out=std::make_shared<DisplayCanvas>();
  // Only the private cache holds a mutable alias, and reuse above requires
  // use_count()==1. Invalidate before any write; reseal only after actual SHA.
  display_detail::CanvasCompositionAuthority::invalidate(*out);
  out->width=layout.canvas_width;out->height=layout.canvas_height;const size_t bytes=size_t(out->width)*out->height*3;
  // A growing vector may geometrically exceed the persistent8MiB lease.
  // Exact replacement preserves the one-canvas bound at every retained size.
  if(bytes>out->rgb.capacity()){std::vector<uint8_t>exact(bytes);out->rgb.swap(exact);}else out->rgb.resize(bytes);
  std::fill(out->rgb.begin(),out->rgb.end(),0);out->source_artifact=a.sha;
  const bool identity=layout.viewport_width==r.w&&layout.viewport_height==r.h;
  std::array<uint32_t,1920>local_x{};std::array<uint32_t,1080>local_row{};
  const uint32_t*xmap=nullptr,*rows=nullptr;
  if(cache){if(identity)++cache->counts.identity_frames;else ++cache->counts.scaled_frames;}
  if(!identity){
   const auto key=std::make_tuple(r.w,r.h,layout.viewport_width,layout.viewport_height);
   auto build=[&](uint32_t*x,uint32_t*row){for(uint32_t i=0;i<layout.viewport_width;++i)x[i]=uint32_t(uint64_t(i)*r.w/layout.viewport_width);for(uint32_t i=0;i<layout.viewport_height;++i)row[i]=uint32_t(uint64_t(i)*r.h/layout.viewport_height)*r.w;};
   if(cache){if(cache->mapped&&cache->key==key)++cache->counts.mapping_hits;else{build(cache->x.data(),cache->row.data());cache->mapped=true;cache->key=key;++cache->counts.mapping_builds;}xmap=cache->x.data();rows=cache->row.data();}
   else{build(local_x.data(),local_row.data());xmap=local_x.data();rows=local_row.data();}
  }
  for(uint32_t y=0;y<layout.viewport_height;++y){
   auto*dest=out->rgb.data()+(size_t(y+layout.viewport_y)*out->width+layout.viewport_x)*3;
   const size_t row=identity?size_t(y)*r.w:rows[y];
   if(identity&&!r.fp32&&r.channels==3){std::memcpy(dest,raw.data+row*3,size_t(r.w)*3);continue;}
   for(uint32_t x=0;x<layout.viewport_width;++x){const size_t pixel=row+(identity?x:xmap[x]);
    if(r.fp32){for(unsigned c=0;c<3;++c){const float f=load_float(raw.data+(size_t(c)*r.w*r.h+pixel)*4);dest[x*3+c]=f<=0?0:f>=255?255:static_cast<uint8_t>(f);}}
    else{const auto*src=raw.data+pixel*r.channels;dest[x*3]=src[0];dest[x*3+1]=src[1];dest[x*3+2]=src[2];}
   }
  }
  vxl_hunet_hash(out->rgb.data(),out->rgb.size(),out->pixel_sha.data());display_detail::CanvasCompositionAuthority::seal(*out);return Result<std::shared_ptr<const DisplayCanvas>>::success(std::move(out));
 }catch(const std::bad_alloc&){return Result<std::shared_ptr<const DisplayCanvas>>::failure({ENOMEM,"DISPLAY","raster page allocation"});}
}
}
namespace vxunified {
Result<std::shared_ptr<const DisplayCanvas>>RasterComposeCache::compose(const PublishedResult&p,size_t i,vxnn::Bytes raw,const DisplayLayout&layout){return compose_raster_impl(p,i,raw,layout,state_.get());}
Result<std::shared_ptr<const DisplayCanvas>>compose_raster_page(const PublishedResult&p,size_t i,vxnn::Bytes raw,const DisplayLayout&layout){auto*cache=display_detail::active_raster_cache();return cache?cache->compose(p,i,raw,layout):compose_raster_impl(p,i,raw,layout,nullptr);}
namespace display_detail {
Result<demo_frame> frame(const PublishedResult&p,const DisplayCanvas&canvas,const DisplayLayout&l,uint32_t w,uint32_t h){
 if(!w||!h||w>1920||h>1080||l.canvas_width!=w||l.canvas_height!=h||canvas.width!=w||canvas.height!=h||canvas.rgb.size()!=uint64_t(w)*h*3||!l.source_width||!l.source_height||!l.viewport_width||!l.viewport_height||uint64_t(l.viewport_x)+l.viewport_width>w||uint64_t(l.viewport_y)+l.viewport_height>h||l.format!=PixelFormat::RGBA8888)return Result<demo_frame>::failure({EINVAL,"DISPLAY","invalid private RGB canvas/layout"});
 bool source=false;for(const auto&a:p.lease.artifacts)source|=a.sha==canvas.source_artifact;
 if(!source||!nonzero_digest(canvas.pixel_sha))return Result<demo_frame>::failure({EBADMSG,"DISPLAY","canvas lacks retained source artifact identity"});
 char actual[65]{},expected[65]{};const char*hex="0123456789abcdef";
 for(size_t i=0;i<32;i++){expected[2*i]=hex[canvas.pixel_sha[i]>>4];expected[2*i+1]=hex[canvas.pixel_sha[i]&15];}
 if(canvas.has_composition_seal())std::memcpy(actual,expected,sizeof(actual));
 else demo_sha_hex(canvas.rgb.data(),canvas.rgb.size(),actual);
 if(std::memcmp(actual,expected,64))return Result<demo_frame>::failure({EBADMSG,"DISPLAY","canvas bytes changed after composition"});
 demo_frame f{};f.width=w;f.height=h;f.stride=w*3;f.bytes=canvas.rgb.size();f.pixels=const_cast<uint8_t*>(canvas.rgb.data());
 // Private copy cookie identity; it does not alter the producer's run receipt.
 const auto&k=p.lease.producer;std::string key=k.boot_id+'\0'+k.client_id+'\0'+k.request_id;
 char request_sha[65]{};demo_sha_hex(key.data(),key.size(),request_sha);std::memcpy(f.run_id,request_sha,32);
 for(size_t i=0;i<32;i++){f.source_result_sha[2*i]=hex[canvas.source_artifact[i]>>4];f.source_result_sha[2*i+1]=hex[canvas.source_artifact[i]&15];}
 std::memcpy(f.sha256,actual,65);return Result<demo_frame>::success(f);
}
}}
