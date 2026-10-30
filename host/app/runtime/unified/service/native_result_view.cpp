#include <vxunified/native_result_view.hpp>
#include <vxunified/sealed_assets.hpp>
#include "hunet_page_view.hpp"
#include <vxl/demo_frame.h>
#include <vxl/hunet.h>
#include "nnrt/package/json.hpp"
#include <cstring>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
namespace vxunified {
namespace {
namespace j=vxnn::json;
bool same_hw(const BackendIdentity&a,const BackendIdentity&b){return a.hardware_contract==b.hardware_contract&&a.bit==b.bit&&a.xsa==b.xsa&&a.compatible==b.compatible&&a.device_generation==b.device_generation&&a.provider_flags==b.provider_flags&&a.board_identity_verified==b.board_identity_verified;}
struct Asset {uint64_t bytes=0;Digest sha{};};
struct CanvasHold {std::shared_ptr<HostMemoryLease>charge;std::shared_ptr<const DisplayCanvas>canvas;};
bool hunet_output(const PublishedResult&p){return p.lease.app==AppKind::NN&&!p.lease.artifacts.empty()&&p.lease.artifacts[0].layout=="CHW_F32"&&p.lease.artifacts[0].shape==vxnn::Shape{8105,1,1};}
}
struct NativeResultView::State {
 std::shared_ptr<HostMemoryLease>charge;
 std::weak_ptr<Runtime>runtime;std::shared_ptr<NativeFiles>files;BackendIdentity hardware;
 vxnn::EvidenceScope scope;std::string boot;mutable std::mutex mutex;
 std::vector<uint8_t>source,table,atlas;demo_frame frame{};demo_labels labels{};
 Digest input{};std::set<Digest>models;bool has_hunet=false;
 std::shared_ptr<hunet_page_detail::PageView>page_view;
 ~State(){demo_labels_close(&labels);}
 Result<std::optional<ResultArtifact>>processed_input(const PublishedResult&p)const{
  using R=Result<std::optional<ResultArtifact>>;
  try{
   const auto&b=*p.binding;const ResultArtifact*record=nullptr,*tensor=nullptr;
   const auto&output=p.lease.artifacts[0];if(!files->belongs_to_request(p.lease.producer,output))return R::failure({EXDEV,"NATIVE_VIEW","output does not belong to the claimed producer request"});auto slash=output.path.rfind('/');
   if(slash==std::string::npos)return R::failure({EINVAL,"NATIVE_VIEW","same request artifact directory required"});
   const auto prefix=output.path.substr(0,slash+1);
   for(const auto&a:p.lease.artifacts){
    auto at=a.path.rfind('/');const auto leaf=a.path.substr(at==std::string::npos?0:at+1);
    if(leaf!="input-record.json"&&leaf!="request-input.f32")continue;
    if(a.path!=prefix+leaf)return R::failure({EXDEV,"NATIVE_VIEW","another request cannot lend input provenance or tensor"});
    auto&slot=leaf=="input-record.json"?record:tensor;if(slot)return R::failure({EINVAL,"NATIVE_VIEW","duplicate input provenance or tensor"});slot=&a;
   }
   if(!record){if(!tensor&&b.input==input)return R::success(std::nullopt);return R::failure({ENODATA,"NATIVE_VIEW","new input requires its actual authoritative request record"});}
   if(record->layout!="JSON_UTF8"||!record->bytes||record->bytes>(128u<<10)||record->shape!=vxnn::Shape{record->bytes})return R::failure({EINVAL,"NATIVE_VIEW","bounded input provenance JSON required"});
   auto rt=runtime.lock();if(!rt)return R::failure({ESHUTDOWN,"NATIVE_VIEW","input provenance Runtime expired"});
   auto charge=rt->reserve_host_memory(1u<<20);if(!charge)return R::failure(*charge.error);
   auto raw=files->read(*record);if(!raw)return R::failure(*raw.error);auto tree=j::parse(std::string(raw.value->begin(),raw.value->end()));auto*o=tree.get();
   j::fields(o,{"format","kind","model","plan","tensor_sha","bytes","source_sha","source_format","preprocessing","preprocessing_id","dtype","layout","shape","input_artifact","gpu_raw_cycles","numeric"});
   j::check(j::str(j::member(o,"format"))=="nn-input-record-v1"&&j::digest(j::member(o,"model"))==p.lease.model&&j::digest(j::member(o,"plan"))==p.lease.plan&&j::digest(j::member(o,"tensor_sha"))==b.input&&j::u64(j::member(o,"bytes"))==b.input_bytes,"request provenance model/plan/input identity differs");
   j::check(!j::member(o,"gpu_raw_cycles")&&j::str(j::member(o,"numeric"))=="NOT_EVALUATED","input record cannot promote GPU or numeric evidence");
   const auto kind=j::str(j::member(o,"kind"));
   if(kind=="FROZEN_REGRESSION_INPUT"){j::check(b.input==input&&!tensor,"frozen source requires original input and no uploaded tensor");return R::success(std::nullopt);}
   j::check((kind=="UPLOADED_IMAGE_TENSOR"||kind=="REVIEWED_FIXED_PAGE_TENSOR")&&tensor,"uploaded input requires the actual request tensor");
   j::check(tensor->layout=="CHW_F32"&&tensor->shape==vxnn::Shape{3,128,128}&&tensor->bytes==196608&&tensor->sha==b.input,"actual processed tensor extent/SHA differs execution input");
   const std::string policy=kind=="REVIEWED_FIXED_PAGE_TENSOR"?"HUNET_REVIEWED_P0_FROZEN_V1":"HUNET_P0_RGB_OTSU_AREA_PAD128_F32_V1";const auto format=j::str(j::member(o,"source_format"));
   j::check(nonzero_digest(j::digest(j::member(o,"source_sha")))&&(format=="PNG"||format=="JPEG"||format=="BMP")&&j::str(j::member(o,"preprocessing"))==policy&&j::digest(j::member(o,"preprocessing_id"))==vxnn::content_digest(policy),"authoritative supported source/preprocessing provenance required");
   j::check(j::str(j::member(o,"dtype"))=="FP32"&&j::str(j::member(o,"layout"))=="CHW_F32"&&j::str(j::member(o,"input_artifact"))=="request-input.f32","processed input encoding/artifact binding differs");
   auto*shape=j::member(o,"shape");j::check(j::count(shape,3)==3&&j::u32(j::at(shape,0))==3&&j::u32(j::at(shape,1))==128&&j::u32(j::at(shape,2))==128,"processed input dimensions differ");
   return R::success(std::optional<ResultArtifact>{*tensor});
  }catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_VIEW","bounded input provenance allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"NATIVE_VIEW",e.what()});}
 }
 Result<void>validate(const PublishedResult&p,std::optional<ResultArtifact>*processed=nullptr)const{
  auto rt=runtime.lock();if(!rt||!files->owns_runtime(rt.get())||!same_hw(hardware,rt->identity())||scope!=rt->evidence_scope())return Result<void>::failure({ESHUTDOWN,"NATIVE_VIEW","original Runtime/files/scope expired or changed"});
  auto valid=validate_published_result(p);if(!valid)return valid;
  if(p.lease.scope!=scope||p.lease.hardware!=hardware.hardware_contract||p.lease.producer.boot_id!=boot||!p.binding)return Result<void>::failure({EXDEV,"NATIVE_VIEW","bound same-boot producer/result scope required"});
  const auto&b=*p.binding;
  if(b.hardware!=hardware.hardware_contract||b.bit!=hardware.bit||b.xsa!=hardware.xsa||b.compatible!=hardware.compatible||b.device_generation!=hardware.device_generation||b.provider_flags!=hardware.provider_flags)return Result<void>::failure({EXDEV,"NATIVE_VIEW","producer full hardware identity differs"});
  if(hunet_page_detail::is_page(p)&&(!has_hunet||!page_view||!models.count(p.lease.model)))return Result<void>::failure({ENODATA,"NATIVE_PAGE_VIEW","trusted full page catalog/model required"});
  if(hunet_output(p)){
   if(!has_hunet||!models.count(p.lease.model)||b.input_bytes!=196608||p.lease.artifacts[0].bytes!=32420)return Result<void>::failure({ESTALE,"NATIVE_VIEW","HUNet model/input/source/label binding differs"});
   auto source=processed_input(p);if(!source)return Result<void>::failure(*source.error);if(processed)*processed=*source.value;
  }
  return Result<void>::success();
 }
};
NativeResultView::NativeResultView(std::shared_ptr<State>s):state_(std::move(s)){}
Result<std::shared_ptr<NativeResultView>>NativeResultView::create(const StartupContext&c,std::shared_ptr<NativeFiles>files,std::optional<NativeHunetViewSpec>spec){
 using R=Result<std::shared_ptr<NativeResultView>>;
 try{
  auto rt=c.runtime.lock();if(!rt||!files||!files->owns_runtime(rt.get())||c.results.lock()!=rt->trusted_results()||!same_hw(c.identity,rt->identity())||c.scope!=rt->evidence_scope()||c.boot_id.empty())return R::failure({EXDEV,"NATIVE_VIEW","same common Runtime/result/files/hardware/scope/boot required"});
  auto charge=rt->reserve_host_memory(32ULL<<20);if(!charge)return R::failure(*charge.error);
  auto s=std::make_shared<State>();s->charge=*charge.value;s->runtime=rt;s->files=std::move(files);s->hardware=c.identity;s->scope=c.scope;s->boot=c.boot_id;
  if(spec){
   if(!s8_provider(c.identity)||spec->models.empty()||spec->models.size()>64||!nonzero_digest(spec->manifest))return R::failure({EINVAL,"NATIVE_VIEW","bounded trusted S8 HUNet binding required"});
   for(const auto&model:spec->models)if(!nonzero_digest(model)||!s->models.insert(model).second)return R::failure({EINVAL,"NATIVE_VIEW","distinct nonzero admitted model identities required"});
   auto raw=read_sealed_native_asset(spec->directory+"/manifest.json",65536);if(!raw)return R::failure(*raw.error);
   if(vxnn::content_digest(*raw.value)!=spec->manifest)return R::failure({ESTALE,"NATIVE_VIEW","sealed view manifest SHA differs"});
   auto tree=j::parse(*raw.value);auto*o=tree.get();
   j::fields(o,{"format","version","source_freeze_manifest","source","input_sha256","classes_sha256","font_sha256","files","numeric_policy","gpu_raw_cycles"});
   j::check(j::str(j::member(o,"format"))=="hunet-view-v1"&&j::u32(j::member(o,"version"))==1&&nonzero_digest(j::digest(j::member(o,"source_freeze_manifest"))),"original sealed HUNet view format/source identity required");
   j::check(j::str(j::member(o,"numeric_policy"))=="PRESERVE_PRODUCER_EVALUATION_AND_HISTORY"&&!j::member(o,"gpu_raw_cycles"),"asset binding cannot assert GPU or numeric acceptance");
   s->input=j::digest(j::member(o,"input_sha256"));j::check(nonzero_digest(s->input),"exact neural input identity required");
   auto classes=j::str(j::member(o,"classes_sha256")),font=j::str(j::member(o,"font_sha256"));j::digest(j::member(o,"classes_sha256"));j::digest(j::member(o,"font_sha256"));
   auto*source=j::member(o,"source");j::fields(source,{"width","height","stride","layout","roi","roi_index"});
   const auto w=j::u32(j::member(source,"width")),h=j::u32(j::member(source,"height")),stride=j::u32(j::member(source,"stride"));
   j::check(w&&h&&w<=1920&&h<=1080&&stride==w*3&&j::str(j::member(source,"layout"))=="RGB888","bounded original RGB source layout required");
   auto*roi=j::member(source,"roi");j::check(j::count(roi,4)==4,"original ROI extent required");uint32_t box[4];for(unsigned i=0;i<4;++i)box[i]=j::u32(j::at(roi,i));
   j::check(box[0]<box[2]&&box[1]<box[3]&&box[2]<=w&&box[3]<=h,"ROI must stay in original source image");
   const demo_bbox bbox{int32_t(box[0]),int32_t(box[1]),int32_t(box[2]),int32_t(box[3]),w,h,j::u32(j::member(source,"roi_index"))};
   auto*entries=j::member(o,"files");j::check(j::count(entries,3)==3,"exact source/labels/glyph asset set required");std::map<std::string,Asset>assets;
   for(size_t i=0;i<3;++i){auto*row=j::at(entries,i);j::fields(row,{"path","bytes","sha256"});auto name=j::str(j::member(row,"path"));j::check(name=="source.rgb"||name=="labels.bin"||name=="glyphs-u8.bin","only original fixed asset leaves admitted");j::check(assets.emplace(name,Asset{j::u64(j::member(row,"bytes")),j::digest(j::member(row,"sha256"))}).second,"asset leaf duplicated");}
   auto load=[&](const char*name,uint64_t expected,std::vector<uint8_t>&to)->Result<void>{
    auto it=assets.find(name);if(it==assets.end()||it->second.bytes!=expected)return Result<void>::failure({EINVAL,"NATIVE_VIEW","original asset extent differs"});
    auto bytes=read_sealed_native_asset(spec->directory+"/"+name,size_t(expected));if(!bytes)return Result<void>::failure(*bytes.error);
    if(bytes.value->size()!=expected||vxnn::content_digest(*bytes.value)!=it->second.sha)return Result<void>::failure({ESTALE,"NATIVE_VIEW","original asset content differs sealed manifest"});
    to.assign(bytes.value->begin(),bytes.value->end());return Result<void>::success();
   };
   auto a=load("source.rgb",uint64_t(stride)*h,s->source);if(!a)return R::failure(*a.error);
   auto b=load("labels.bin",96+8105ULL*32,s->table);if(!b)return R::failure(*b.error);
   auto d=load("glyphs-u8.bin",8105ULL*1024,s->atlas);if(!d)return R::failure(*d.error);
   int rc=demo_labels_bind(s->table.data(),s->table.size(),s->atlas.data(),s->atlas.size(),classes.c_str(),font.c_str(),&s->labels);if(rc)return R::failure({-rc,"NATIVE_VIEW","original decoder rejected class/font/glyph table"});
   s->labels.bbox=bbox;s->frame.width=w;s->frame.height=h;s->frame.stride=stride;s->frame.bytes=s->source.size();s->frame.pixels=s->source.data();rc=demo_frame_hash(&s->frame);if(rc)return R::failure({-rc,"NATIVE_VIEW","original source frame hash failed"});s->has_hunet=true;
   if(!spec->page_presets.empty()){auto pages=hunet_page_detail::PageView::create(spec->page_presets,s->models);if(!pages)return R::failure(*pages.error);s->page_view=*pages.value;}
  }
  return R::success(std::shared_ptr<NativeResultView>(new NativeResultView(std::move(s))));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_VIEW","bounded shared-budget view allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"NATIVE_VIEW",e.what()});}
}
Result<DisplayLayout>NativeResultView::layout(const PublishedResult&p,uint32_t w,uint32_t h)const{
 auto&s=*state_;std::optional<ResultArtifact>processed;auto valid=s.validate(p,&processed);if(!valid)return Result<DisplayLayout>::failure(*valid.error);
 if(hunet_page_detail::is_page(p)){auto rt=s.runtime.lock();if(!rt)return Result<DisplayLayout>::failure({ESHUTDOWN,"NATIVE_PAGE_VIEW","Runtime expired"});auto charge=rt->reserve_host_memory(4u<<20);if(!charge)return Result<DisplayLayout>::failure(*charge.error);return s.page_view->layout(p,s.files,w,h);}
 if(!hunet_output(p))return raster_page_layout(p.lease,0,w,h);
 if(w<640||h<480||w>1920||h>1080)return Result<DisplayLayout>::failure({ERANGE,"NATIVE_VIEW","original bounded HUNet panel required"});
 return Result<DisplayLayout>::success({w,h,processed?128:s.frame.width,processed?128:s.frame.height,0,0,w*3/4,h,PixelFormat::RGBA8888});
}
Result<std::shared_ptr<const DisplayCanvas>>NativeResultView::compose(const PublishedResult&p,const DisplayLayout&l)const{
 using R=Result<std::shared_ptr<const DisplayCanvas>>;auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);
 try{
  if(hunet_page_detail::is_page(p)){auto valid=s.validate(p);if(!valid)return R::failure(*valid.error);auto rt=s.runtime.lock();if(!rt)return R::failure({ESHUTDOWN,"NATIVE_PAGE_VIEW","Runtime expired"});return s.page_view->compose(p,l,rt,s.files,s.labels);}
  auto expected=layout(p,l.canvas_width,l.canvas_height);if(!expected)return R::failure(*expected.error);auto rt=s.runtime.lock();if(!rt)return R::failure({ESHUTDOWN,"NATIVE_VIEW","common Runtime expired"});
  std::optional<ResultArtifact>processed;if(hunet_output(p)){auto source=s.processed_input(p);if(!source)return R::failure(*source.error);processed=*source.value;}
  // Retained canvas aliases own the common charge through final retirement.
  const auto bytes=j::add(j::add(p.lease.artifacts[0].bytes,j::mul(j::mul(l.canvas_width,l.canvas_height),3)),65536+(processed?196608+49152:0));
  auto charge=rt->reserve_host_memory(bytes);if(!charge)return R::failure(*charge.error);auto raw=s.files->read(p.lease.artifacts[0]);if(!raw)return R::failure(*raw.error);
  auto render=[&]()->R{
   if(!hunet_output(p))return compose_raster_page(p,0,{raw.value->data(),raw.value->size()},l);
   // Native NN currently publishes NOT_EVALUATED. Preserve it; an evaluated
   // producer must provide its original numeric evidence instead of a guess.
   if(p.numeric!=vxnn::NumericClass::NOT_EVALUATED)return R::failure({ENODATA,"NATIVE_VIEW","evaluated HUNet producer requires original numeric evidence"});
   std::vector<uint8_t>preview;demo_frame frame=s.frame;demo_labels labels=s.labels;
   if(processed){
    auto tensor=s.files->read(*processed);if(!tensor)return R::failure(*tensor.error);preview.resize(128u*128u*3u);
    for(size_t channel=0;channel<3;channel++)for(size_t i=0;i<128u*128u;i++){
     const auto*in=tensor.value->data()+(channel*128u*128u+i)*4;uint32_t bits=uint32_t(in[0])|uint32_t(in[1])<<8|uint32_t(in[2])<<16|uint32_t(in[3])<<24;float value;std::memcpy(&value,&bits,4);
     if(!std::isfinite(value)||value< -1.f||value>1.f)return R::failure({EDOM,"NATIVE_VIEW","processed HUNet tensor must stay finite in[-1,1]"});
     preview[3*i+channel]=uint8_t(std::lround((value+1.f)*127.5f));
    }
    frame={};frame.width=frame.height=128;frame.stride=384;frame.bytes=preview.size();frame.pixels=preview.data();auto input_hex=vxnn::digest_hex(p.binding->input);std::memcpy(frame.raw_source_sha,input_hex.c_str(),65);
    int checked=demo_frame_hash(&frame);if(checked)return R::failure({-checked,"NATIVE_VIEW","processed preview hash failed"});labels.bbox={0,0,128,128,128,128,0};
   }
   std::vector<float>logits(8105);std::memcpy(logits.data(),raw.value->data(),raw.value->size());demo_hunet_result view{};
   int rc=demo_hunet_decode(logits.data(),logits.size(),&labels,&view);if(rc)return R::failure({-rc,"NATIVE_VIEW","original top5/glyph decoder failed"});
   auto id=vxnn::digest_hex(vxnn::content_digest(p.lease.producer.boot_id+'\0'+p.lease.producer.client_id+'\0'+p.lease.producer.request_id)).substr(0,32);std::memcpy(view.run_id,id.c_str(),33);
   demo_sha_hex(p.raw_legacy_json.data(),p.raw_legacy_json.size(),view.source_result_sha);view.source_frame=&frame;view.view_scope=s.scope==vxnn::EvidenceScope::REAL_BOARD?DEMO_VIEW_LIVE:DEMO_VIEW_OFFLINE;view.numeric_class=DEMO_NUMERIC_UNEVALUATED;
   auto page=compose_hunet_page(p,0,{raw.value->data(),raw.value->size()},view,l);if(!page||!processed)return page;
   // The compositor allocated this non-const canvas; annotate before exposing
   // its immutable alias. This is processed input, never the original image.
   auto canvas=std::const_pointer_cast<DisplayCanvas>(*page.value);demo_frame annotated{};annotated.width=canvas->width;annotated.height=canvas->height;annotated.stride=canvas->width*3;annotated.bytes=canvas->rgb.size();annotated.pixels=canvas->rgb.data();
   rc=demo_draw_ascii(&annotated,"PROCESSED INPUT 128X128",16,12,1,0xffbd60);if(rc)return R::failure({-rc,"NATIVE_VIEW","processed preview annotation failed"});
   vxl_hunet_hash(canvas->rgb.data(),canvas->rgb.size(),canvas->pixel_sha.data());return page;
  };
  auto result=render();if(!result)return result;auto hold=std::make_shared<CanvasHold>();hold->charge=*charge.value;hold->canvas=*result.value;
  return R::success(std::shared_ptr<const DisplayCanvas>(hold,hold->canvas.get()));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_VIEW","bounded composed-page allocation"});}catch(const std::exception&e){return R::failure({EINVAL,"NATIVE_VIEW",e.what()});}
}
}
