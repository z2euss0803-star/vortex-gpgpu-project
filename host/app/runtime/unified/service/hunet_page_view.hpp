#pragma once
#include <vxunified/hunet_page_identity.hpp>
#include <vxunified/sealed_assets.hpp>
#include "hunet_page_json.hpp"
#include <vxl/demo_frame.h>
#include <vxl/hunet.h>
#include <map>
#include <set>
#include <cstring>
#include <cstdio>
namespace vxunified { namespace hunet_page_detail {
namespace j=vxnn::json;
inline bool is_page(const PublishedResult&p){return p.lease.app==AppKind::NN&&!p.lease.artifacts.empty()&&p.lease.artifacts[0].layout=="HUNET_PAGE_F32";}
struct PageFailure {vxnn::Error error;};
template<class T>T take(Result<T>r){if(!r){throw PageFailure{*r.error};}return std::move(*r.value);}
inline bool identifier(const std::string&s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';});}
struct Word {vxnn::RequestInput input;std::string roi;std::array<uint32_t,4>box{};};
struct Page {std::string id,title;Digest source{},rgb{};uint32_t width=0,height=0,stride=0;uint64_t source_bytes=0,rgb_bytes=0;std::vector<Word>words;};
struct Catalog {Digest manifest{};std::map<std::string,Page>pages;};
struct Binding {std::shared_ptr<const Catalog>catalog;Digest model{},base{};};
struct Top {Digest logits{};std::array<uint32_t,5>ids{};std::array<float,5>scores{};};
struct Index {const Page*page=nullptr;std::vector<Top>words;};
inline std::shared_ptr<const Catalog>load_catalog(const NativeHunetPagePreset&spec){
 auto raw=take(read_sealed_native_asset(spec.directory+"/manifest.json",256u<<10));
 j::check(vxnn::content_digest(raw)==spec.manifest,"installed page catalog SHA differs trusted pin");
 auto seal=take(read_sealed_native_asset(spec.directory+"/manifest.sha256",65));j::check(seal==vxnn::digest_hex(spec.manifest)+"\n","page catalog seal differs");
 auto tree=j::parse(raw);auto*o=tree.get();j::fields(o,{"format","version","source_manifest_sha256","preprocessing","preprocessing_id","converter_sha256","dtype","layout","shape","pages"});
 const auto version=j::u32(j::member(o,"version"));j::check((version==1&&j::str(j::member(o,"format"))=="hunet-fixed-inputs-v1")||(version==2&&j::str(j::member(o,"format"))=="hunet-fixed-inputs-v2"),"installed reviewed catalog version required");
 j::check(nonzero_digest(j::digest(j::member(o,"source_manifest_sha256")))&&nonzero_digest(j::digest(j::member(o,"converter_sha256"))),"reviewed source/converter identities required");
 const auto policy=j::str(j::member(o,"preprocessing"));const auto policy_id=vxnn::content_digest(policy);j::check(policy=="HUNET_REVIEWED_P0_FROZEN_V1"&&j::digest(j::member(o,"preprocessing_id"))==policy_id,"reviewed frozen preprocessing required");
 auto*shape=j::member(o,"shape");j::check(j::str(j::member(o,"dtype"))=="FP32"&&j::str(j::member(o,"layout"))=="CHW_F32"&&j::count(shape,3)==3&&j::u32(j::at(shape,0))==3&&j::u32(j::at(shape,1))==128&&j::u32(j::at(shape,2))==128,"fixed CHW128 catalog required");
 auto out=std::make_shared<Catalog>();out->manifest=spec.manifest;std::set<std::string>page_ids,inputs;size_t total=0;auto*pages=j::member(o,"pages");auto count=j::count(pages,8);j::check(count>0,"reviewed pages required");
 for(size_t n=0;n<count;n++){
  auto*row=j::at(pages,n);j::fields(row,{"id","title","source_sha256","items","image"});Page p;p.id=j::str(j::member(row,"id"));p.title=j::str(j::member(row,"title"));p.source=j::digest(j::member(row,"source_sha256"));
  j::check(identifier(p.id)&&page_ids.insert(p.id).second&&!p.title.empty()&&p.title.size()<=128&&nonzero_digest(p.source),"distinct bounded reviewed page identity required");
  if(version==2){
   auto*im=j::member(row,"image");j::fields(im,{"width","height","stride","source_file","source_bytes","source_format","rgb_file","rgb_bytes","rgb_sha256","conversion"});
   p.width=j::u32(j::member(im,"width"));p.height=j::u32(j::member(im,"height"));p.stride=j::u32(j::member(im,"stride"));p.source_bytes=j::u64(j::member(im,"source_bytes"));p.rgb_bytes=j::u64(j::member(im,"rgb_bytes"));p.rgb=j::digest(j::member(im,"rgb_sha256"));const auto fmt=j::str(j::member(im,"source_format"));
   j::check(p.width&&p.height&&p.width<=16384&&p.height<=16384&&p.stride==uint64_t(p.width)*3&&p.rgb_bytes==uint64_t(p.stride)*p.height&&p.rgb_bytes<=DEMO_MAX_ASSET&&p.source_bytes&&p.source_bytes<=DEMO_MAX_ASSET&&nonzero_digest(p.rgb),"bounded full page source dimensions and extents required");
   j::check(j::str(j::member(im,"source_file"))==p.id+".source"&&j::str(j::member(im,"rgb_file"))==p.id+".rgb"&&(fmt=="WEBP"||fmt=="JPEG"||fmt=="PNG")&&j::str(j::member(im,"conversion"))=="RGB_NO_GEOMETRY_CHANGE_V1","original page image leaves/conversion required");
  }else{j::check(!j::has(row,"image"),"legacy v1 catalog cannot declare full page image");}
  auto*items=j::member(row,"items");auto words=j::count(items,128);j::check(words&&total+words<=128,"at most128 reviewed crops per installed catalog");total+=words;
  for(size_t i=0;i<words;i++){
   auto*item=j::at(items,i);j::fields(item,{"input_id","roi_id","bbox_xyxy","source_file","source_format","source_bytes","source_sha256","tensor_file","tensor_bytes","tensor_sha256"});Word w;w.input.id=j::str(j::member(item,"input_id"));w.roi=j::str(j::member(item,"roi_id"));w.input.source_format=j::str(j::member(item,"source_format"));w.input.source_sha=j::digest(j::member(item,"source_sha256"));w.input.tensor_sha=j::digest(j::member(item,"tensor_sha256"));w.input.preprocessing=policy;w.input.preprocessing_id=policy_id;w.input.shape={3,128,128};
   j::check(identifier(w.roi)&&identifier(w.input.id)&&w.input.id=="example-"+w.roi&&inputs.insert(w.input.id).second&&w.input.source_format=="PNG"&&nonzero_digest(w.input.source_sha)&&nonzero_digest(w.input.tensor_sha),"distinct reviewed crop/tensor identities required");
   j::check(j::str(j::member(item,"source_file"))==w.input.id+".source"&&j::str(j::member(item,"tensor_file"))==w.input.id+".f32"&&j::u64(j::member(item,"source_bytes"))&&j::u64(j::member(item,"source_bytes"))<=(1u<<20)&&j::u64(j::member(item,"tensor_bytes"))==196608,"reviewed crop leaf/extent differs");
   auto*b=j::member(item,"bbox_xyxy");j::check(j::count(b,4)==4,"reviewed bbox required");for(size_t k=0;k<4;k++){w.box[k]=j::u32(j::at(b,k));}
   j::check(w.box[0]<w.box[2]&&w.box[1]<w.box[3]&&w.box[2]<=16384&&w.box[3]<=16384&&(version==1||(w.box[2]<=p.width&&w.box[3]<=p.height)),"reviewed crop must remain in original page");p.words.push_back(std::move(w));
  }
  // v1 remains a valid legacy single-word catalog; it adds no page entry.
  if(version==2){out->pages.emplace(p.id,std::move(p));}
 }
 return out;
}
inline float number(json_object*o){
 j::check(o&&(json_object_is_type(o,json_type_double)||json_object_is_type(o,json_type_int)),"raw score must be numeric");double x=json_object_get_double(o);float f=float(x);j::check(std::isfinite(x)&&std::isfinite(f)&&double(f)==x,"raw FP32 score must preserve its exact finite value");
 // Integer JSON can lose low bits through json-c's double conversion. Check
 // its original 64-bit value with a defined-range conversion as well.
 if(json_object_is_type(o,json_type_int)){
  if(json_object_get_string(o)[0]=='-'){const auto exact=json_object_get_int64(o);j::check(double(f)>=-9223372036854775808.0&&double(f)<9223372036854775808.0&&int64_t(f)==exact,"integer raw score differs exact FP32");}
  else{const auto exact=json_object_get_uint64(o);j::check(double(f)>=0.0&&double(f)<18446744073709551616.0&&uint64_t(f)==exact,"integer raw score differs exact FP32");}
 }
 return f;
}
inline float fp32(const uint8_t*p){uint32_t v=uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;float f;std::memcpy(&f,&v,4);return f;}
inline bool same_layout(const DisplayLayout&a,const DisplayLayout&b){return a.canvas_width==b.canvas_width&&a.canvas_height==b.canvas_height&&a.source_width==b.source_width&&a.source_height==b.source_height&&a.viewport_x==b.viewport_x&&a.viewport_y==b.viewport_y&&a.viewport_width==b.viewport_width&&a.viewport_height==b.viewport_height&&a.format==b.format;}
inline DisplayLayout page_layout(const Page&p,uint32_t w,uint32_t h){j::check(w>=640&&h>=480&&w<=1920&&h<=1080,"bounded final page canvas640x480..1920x1080 required");return {w,h,p.width,p.height,0,0,w*3/4,h,PixelFormat::RGBA8888};}
inline void pixel(demo_frame&f,uint32_t x,uint32_t y,uint32_t colour){if(x>=f.width||y>=f.height){return;}auto*d=f.pixels+size_t(y)*f.stride+3*x;d[0]=uint8_t(colour>>16);d[1]=uint8_t(colour>>8);d[2]=uint8_t(colour);}
inline void small_text(demo_frame&frame,const std::string&s,demo_rect region,uint32_t colour){
 j::check(!s.empty()&&s.size()<=128&&region.width&&region.height,"bounded page annotation required");
 std::vector<uint8_t>rgb(s.size()*8*16*3);demo_frame text{};text.width=uint32_t(s.size()*8);text.height=16;text.stride=text.width*3;text.bytes=rgb.size();text.pixels=rgb.data();j::check(!demo_draw_ascii(&text,s.c_str(),0,0,1,colour),"page ASCII rasterization failed");
 uint32_t w=std::min(region.width,text.width),h=std::max(1u,uint32_t(uint64_t(16)*w/text.width));if(h>region.height){h=region.height;w=std::max(1u,uint32_t(uint64_t(text.width)*h/16));}
 const uint32_t ox=region.x+(region.width-w)/2,oy=region.y+(region.height-h)/2;
 for(uint32_t y=0;y<h;y++){for(uint32_t x=0;x<w;x++){const auto*src=text.pixels+size_t(uint64_t(y)*16/h)*text.stride+size_t(uint64_t(x)*text.width/w)*3;if(src[0]||src[1]||src[2]){pixel(frame,ox+x,oy+y,(uint32_t(src[0])<<16)|(uint32_t(src[1])<<8)|src[2]);}}}
}
inline void glyph(demo_frame&frame,const demo_glyph&g,demo_rect region){
 if(!g.available){small_text(frame,g.fallback,region,0xffffff);return;}
 uint32_t n=std::min({region.width,region.height,64u});j::check(n>0&&g.pixels&&g.width&&g.height,"page glyph extent required");uint32_t ox=region.x+(region.width-n)/2,oy=region.y+(region.height-n)/2;
 for(uint32_t y=0;y<n;y++){for(uint32_t x=0;x<n;x++){uint8_t alpha=g.pixels[size_t(uint64_t(y)*g.height/n)*g.stride+uint64_t(x)*g.width/n];auto*dst=frame.pixels+size_t(oy+y)*frame.stride+3*(ox+x);for(unsigned c=0;c<3;c++){dst[c]=uint8_t((255u*alpha+unsigned(dst[c])*(255u-alpha)+127u)/255u);}}}
}
struct PageCanvasHold {std::shared_ptr<HostMemoryLease>charge;std::shared_ptr<DisplayCanvas>canvas;};
class PageView {
 std::vector<Binding>bindings_;
 Index inspect(const PublishedResult&p,const std::shared_ptr<NativeFiles>&files)const{
  j::check(is_page(p)&&p.binding&&p.numeric==vxnn::NumericClass::NOT_EVALUATED,"final unevaluated bound HUNet page required");
  const auto&arts=p.lease.artifacts;j::check(arts.size()==4||arts.size()==5,"four fixed final page artifacts required");const char*names[]={"page-logits.f32","page-result.json","page-source.rgb","page-original.source","plan-inventory.json"};
  for(size_t i=0;i<arts.size();i++){auto at=arts[i].path.rfind('/');j::check(at!=std::string::npos&&arts[i].path.substr(at+1)==names[i]&&files->belongs_to_request(p.lease.producer,arts[i]),"page artifacts must belong to original producer and fixed ordinal/leaf");}
  const auto&a=arts[1];j::check(a.layout=="JSON_UTF8"&&a.bytes<=(256u<<10)&&a.shape==vxnn::Shape{a.bytes},"bounded actual final page JSON artifact required");
  auto raw=take(files->read(a));auto tree=parse_index(std::string(raw.begin(),raw.end()));auto*o=tree.get();
  j::fields(o,{"format","page_id","title","catalog_sha256","source_sha256","width","height","rgb_sha256","model","base_plan","plan","input_sha256","total","completed","order","score_kind","numeric_class","words","performance"});
  j::check(j::str(j::member(o,"format"))=="hunet-page-result-v1"&&j::str(j::member(o,"order"))=="REVIEWED_MANIFEST_ORDER"&&j::str(j::member(o,"score_kind"))=="RAW_LOGIT_NOT_PROBABILITY"&&j::str(j::member(o,"numeric_class"))=="NOT_EVALUATED","final index format/order/raw score/numeric contract differs");
  const auto catalog=j::digest(j::member(o,"catalog_sha256"));const auto page_id=j::str(j::member(o,"page_id"));const Binding*binding=nullptr;const Page*page=nullptr;
  for(const auto&b:bindings_){if(b.model==p.lease.model&&b.catalog->manifest==catalog){auto at=b.catalog->pages.find(page_id);if(at!=b.catalog->pages.end()){j::check(!binding,"ambiguous trusted page model/catalog");binding=&b;page=&at->second;}}}
  j::check(binding&&page,"page must match an installed model/catalog/page");const auto n=page->words.size();
  j::check(j::str(j::member(o,"title"))==page->title&&j::digest(j::member(o,"source_sha256"))==page->source&&j::digest(j::member(o,"rgb_sha256"))==page->rgb&&j::u32(j::member(o,"width"))==page->width&&j::u32(j::member(o,"height"))==page->height,"actual reviewed original source/frame identity differs");
  j::check(j::u64(j::member(o,"total"))==n&&j::u64(j::member(o,"completed"))==n&&j::digest(j::member(o,"model"))==binding->model&&j::digest(j::member(o,"base_plan"))==binding->base,"complete page/model/base plan required");
  std::vector<vxnn::RequestInput>inputs;for(const auto&w:page->words){inputs.push_back(w.input);}auto input=take(hunet_page_input_id(inputs));auto plan=take(hunet_page_plan_id(binding->base,inputs));
  j::check(p.binding->input==input&&p.binding->input_bytes==n*196608&&p.lease.plan==plan&&p.binding->plan==plan&&j::digest(j::member(o,"input_sha256"))==input&&j::digest(j::member(o,"plan"))==plan,"ordered page input/plan identity differs execution binding");
  j::check(arts[0].layout=="HUNET_PAGE_F32"&&arts[0].shape==vxnn::Shape{n,8105,1}&&arts[0].bytes==n*32420&&arts[2].layout=="RGB888"&&arts[2].shape==vxnn::Shape{page->height,page->width,3}&&arts[2].bytes==page->rgb_bytes&&arts[2].sha==page->rgb&&arts[3].layout=="OPAQUE_U8"&&arts[3].shape==vxnn::Shape{page->source_bytes}&&arts[3].bytes==page->source_bytes&&arts[3].sha==page->source,"actual page artifact shapes/bytes/SHA differ trusted source");
  if(arts.size()==5){j::check(arts[4].layout=="JSON_UTF8"&&arts[4].bytes<=(256u<<10)&&arts[4].shape==vxnn::Shape{arts[4].bytes},"optional plan inventory extent differs");}
  Index result;result.page=page;auto*words=j::member(o,"words");j::check(j::count(words,128)==n,"all ordered final word records required");
  for(size_t i=0;i<n;i++){
   auto*row=j::at(words,i);j::fields(row,{"index","input_id","roi_id","bbox_xyxy","source_sha256","tensor_sha256","preprocessing","preprocessing_id","plan","logits_sha256","offset","bytes","topk_class_ids","topk_raw_logits","durable_record","begin_ns","end_ns"});const auto&w=page->words[i];
   j::check(j::u64(j::member(row,"index"))==i&&j::str(j::member(row,"input_id"))==w.input.id&&j::str(j::member(row,"roi_id"))==w.roi&&j::digest(j::member(row,"source_sha256"))==w.input.source_sha&&j::digest(j::member(row,"tensor_sha256"))==w.input.tensor_sha&&j::str(j::member(row,"preprocessing"))==w.input.preprocessing&&j::digest(j::member(row,"preprocessing_id"))==w.input.preprocessing_id&&j::digest(j::member(row,"plan"))==vxnn::request_input_plan_id(binding->base,w.input),"ordered crop/tensor/ROI/preprocessing/plan identity differs catalog");
   auto*box=j::member(row,"bbox_xyxy");j::check(j::count(box,4)==4,"final reviewed bbox required");for(size_t k=0;k<4;k++){j::check(j::u32(j::at(box,k))==w.box[k],"final reviewed bbox differs catalog");}
   j::check(j::u64(j::member(row,"offset"))==i*32420&&j::u64(j::member(row,"bytes"))==32420&&j::str(j::member(row,"durable_record"))=="trace.raw#word-"+std::to_string(i),"ordered original logits slice/trace reference differs");
   if(j::has(row,"begin_ns")){j::u64(j::member(row,"begin_ns"));}if(j::has(row,"end_ns")){j::u64(j::member(row,"end_ns"));}if(j::has(row,"begin_ns")&&j::has(row,"end_ns")){j::check(j::u64(j::member(row,"end_ns"))>=j::u64(j::member(row,"begin_ns")),"word time order differs");}
   Top top;top.logits=j::digest(j::member(row,"logits_sha256"));j::check(nonzero_digest(top.logits),"actual word logits SHA required");auto*ids=j::member(row,"topk_class_ids");auto*scores=j::member(row,"topk_raw_logits");j::check(j::count(ids,5)==5&&j::count(scores,5)==5,"exact original top5 required");for(size_t k=0;k<5;k++){top.ids[k]=j::u32(j::at(ids,k));j::check(top.ids[k]<8105,"top5 class outside bound");top.scores[k]=number(j::at(scores,k));}result.words.push_back(top);
  }
  if(j::has(o,"performance")){
   auto*q=j::member(o,"performance");j::fields(q,{"constant_uploads","constant_upload_bytes","input_uploads","input_upload_bytes","data_allocations","args_allocations","words_completed","gpu_raw_cycles"});
   for(auto key:{"constant_uploads","constant_upload_bytes","input_uploads","input_upload_bytes","data_allocations","args_allocations","words_completed"}){j::u64(j::member(q,key));}auto*cycles=j::member(q,"gpu_raw_cycles");if(cycles){j::u64(cycles);}
  }
  return result;
 }
public:
 static Result<std::shared_ptr<PageView>>create(const std::vector<NativeHunetPagePreset>&specs,const std::set<Digest>&models){
  using R=Result<std::shared_ptr<PageView>>;try{
   j::check(specs.size()<=64,"bounded installed page model bindings required");auto out=std::make_shared<PageView>();std::map<std::pair<std::string,Digest>,std::shared_ptr<const Catalog>>cache;std::set<std::pair<Digest,Digest>>bound;
   for(const auto&s:specs){j::check(models.count(s.model)&&nonzero_digest(s.manifest)&&nonzero_digest(s.base_plan)&&bound.insert({s.model,s.manifest}).second,"distinct admitted model/base plan/catalog bindings required");const auto key=std::make_pair(s.directory,s.manifest);auto at=cache.find(key);if(at==cache.end()){j::check(cache.size()<8,"at most8 distinct installed page catalogs");at=cache.emplace(key,load_catalog(s)).first;}out->bindings_.push_back({at->second,s.model,s.base_plan});}
   return R::success(out);
  }catch(const PageFailure&e){return R::failure(e.error);}catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_PAGE_VIEW","bounded page catalog allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"NATIVE_PAGE_VIEW",e.what()});}
 }
 Result<DisplayLayout>layout(const PublishedResult&p,const std::shared_ptr<NativeFiles>&files,uint32_t w,uint32_t h)const{
  using R=Result<DisplayLayout>;try{return R::success(page_layout(*inspect(p,files).page,w,h));}catch(const PageFailure&e){return R::failure(e.error);}catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_PAGE_VIEW","bounded page index allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"NATIVE_PAGE_VIEW",e.what()});}
 }
 Result<std::shared_ptr<const DisplayCanvas>>compose(const PublishedResult&p,const DisplayLayout&l,const std::shared_ptr<Runtime>&rt,const std::shared_ptr<NativeFiles>&files,const demo_labels&labels)const{
  using R=Result<std::shared_ptr<const DisplayCanvas>>;try{
   uint64_t bytes=8u<<20;for(size_t i=0;i<std::min(size_t(4),p.lease.artifacts.size());i++){bytes=j::add(bytes,p.lease.artifacts[i].bytes);}bytes=j::add(bytes,j::mul(j::mul(l.canvas_width,l.canvas_height),3));auto charge=take(rt->reserve_host_memory(bytes));
   auto index=inspect(p,files);const auto&page=*index.page;j::check(same_layout(l,page_layout(page,l.canvas_width,l.canvas_height)),"caller cannot alter final page geometry");
   auto raw=take(files->read(p.lease.artifacts[0]));auto rgb=take(files->read(p.lease.artifacts[2]));auto source=take(files->read(p.lease.artifacts[3]));j::check(source.size()==page.source_bytes,"actual original source extent differs");
   std::vector<demo_glyph>glyphs;std::vector<float>logits(8105);demo_labels bound=labels;
   for(size_t i=0;i<page.words.size();i++){
    const auto*word=raw.data()+i*32420;j::check(vxnn::content_digest(std::string(reinterpret_cast<const char*>(word),32420))==index.words[i].logits,"actual word logits bytes differ original index SHA");for(size_t k=0;k<8105;k++){logits[k]=fp32(word+4*k);}
    const auto&box=page.words[i].box;bound.bbox={int32_t(box[0]),int32_t(box[1]),int32_t(box[2]),int32_t(box[3]),page.width,page.height,uint32_t(i)};demo_hunet_result decoded{};int rc=demo_hunet_decode(logits.data(),logits.size(),&bound,&decoded);if(rc){throw PageFailure{{-rc,"NATIVE_PAGE_VIEW","actual finite logits/top5 decode failed"}};}
    for(size_t k=0;k<5;k++){j::check(decoded.top[k].class_id==index.words[i].ids[k]&&!std::memcmp(&decoded.top[k].score,&index.words[i].scores[k],4),"actual finite logits/top5 differs final index");}glyphs.push_back(decoded.glyphs[0]);
   }
   // All source/index/logits admission is complete before writing any canvas.
   auto out=std::make_shared<DisplayCanvas>();out->width=l.canvas_width;out->height=l.canvas_height;out->source_artifact=p.lease.artifacts[0].sha;out->rgb.resize(size_t(out->width)*out->height*3);
   demo_frame canvas{};canvas.width=out->width;canvas.height=out->height;canvas.stride=out->width*3;canvas.bytes=out->rgb.size();canvas.pixels=out->rgb.data();demo_frame original{};original.width=page.width;original.height=page.height;original.stride=page.stride;original.bytes=rgb.size();original.pixels=rgb.data();demo_rect fit{};j::check(!demo_letterbox(&original,&canvas,{0,0,l.viewport_width,l.viewport_height},&fit),"original full page letterbox failed");
   for(size_t i=0;i<page.words.size();i++){
    const auto&b=page.words[i].box;demo_bbox box{int32_t(b[0]),int32_t(b[1]),int32_t(b[2]),int32_t(b[3]),page.width,page.height,uint32_t(i)};demo_rect mapped{};j::check(!demo_map_bbox(&box,fit,&mapped),"reviewed bbox mapping failed");
    for(uint32_t x=0;x<mapped.width;x++){pixel(canvas,mapped.x+x,mapped.y,0xffbd60);pixel(canvas,mapped.x+x,mapped.y+mapped.height-1,0xffbd60);}for(uint32_t y=0;y<mapped.height;y++){pixel(canvas,mapped.x,mapped.y+y,0xffbd60);pixel(canvas,mapped.x+mapped.width-1,mapped.y+y,0xffbd60);}
    const uint32_t x=std::min(mapped.x,l.viewport_width-16),y=std::min(mapped.y,l.viewport_height-8);small_text(canvas,std::to_string(i+1),{x,y,std::min(24u,l.viewport_width-x),8},0xffbd60);
   }
   const uint32_t x=l.viewport_width,panel=l.canvas_width-x;small_text(canvas,"COMPLETE "+std::to_string(page.words.size())+" WORDS",{x+4,8,panel-8,16},0xffffff);const char*scopes[]={"HOST_PROTOCOL_TEST","OFFLINE","CROSS_COMPILE","IMAGE","REAL_BOARD"};small_text(canvas,scopes[unsigned(p.lease.scope)],{x+4,30,panel-8,16},0xffbd60);small_text(canvas,"NOT_EVALUATED",{x+4,48,panel-8,16},0xffffff);small_text(canvas,"RAW LOGIT",{x+4,66,panel-8,16},0xffffff);
   const uint32_t top=88,available=l.canvas_height-top-8;const size_t n=page.words.size();uint32_t cols=1;while(uint64_t(cols)*cols*available<uint64_t(n)*(panel-8)){++cols;}cols=std::min(cols,uint32_t(n));const uint32_t rows=uint32_t((n+cols-1)/cols),cw=(panel-8)/cols,ch=available/rows;j::check(cw>=8&&ch>=12,"all ordered glyphs must fit final page panel");
   for(size_t i=0;i<n;i++){
    const uint32_t cx=x+4+uint32_t(i%cols)*cw,cy=top+uint32_t(i/cols)*ch,caption=std::max(3u,std::min(16u,ch/5));small_text(canvas,std::to_string(i+1),{cx,cy,cw,caption},0xffbd60);glyph(canvas,glyphs[i],{cx,cy+caption,cw,ch-2*caption});char score[48];std::snprintf(score,sizeof(score),"%.9g",double(index.words[i].scores[0]));small_text(canvas,score,{cx,cy+ch-caption,cw,caption},0xffffff);
   }
   vxl_hunet_hash(out->rgb.data(),out->rgb.size(),out->pixel_sha.data());auto hold=std::make_shared<PageCanvasHold>();hold->charge=std::move(charge);hold->canvas=out;return R::success(std::shared_ptr<const DisplayCanvas>(hold,out.get()));
  }catch(const PageFailure&e){return R::failure(e.error);}catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_PAGE_VIEW","bounded final page composition allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"NATIVE_PAGE_VIEW",e.what()});}
 }
};
}}
