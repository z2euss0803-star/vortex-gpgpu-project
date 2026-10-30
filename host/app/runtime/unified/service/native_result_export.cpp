#include <vxunified/native_result_export.hpp>
#include <vxunified/nn_result_metadata.hpp>
#include <vxunified/nn_input.hpp>
#include <vxl/hunet.h>
#include "nnrt/package/json.hpp"
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
namespace vxunified {
namespace {
namespace j=vxnn::json;
using J=j::Ptr;
J object(){auto*p=json_object_new_object();if(!p)throw std::bad_alloc();return J(p,json_object_put);}
void put(json_object*o,const char*k,json_object*v){if(json_object_object_add(o,k,v)){json_object_put(v);throw std::bad_alloc();}}
void text(json_object*o,const char*k,const std::string&s){auto*v=json_object_new_string_len(s.data(),int(s.size()));if(!v)throw std::bad_alloc();put(o,k,v);}
void number(json_object*o,const char*k,uint64_t n){auto*v=json_object_new_uint64(n);if(!v)throw std::bad_alloc();put(o,k,v);}
void boolean(json_object*o,const char*k,bool b){auto*v=json_object_new_boolean(b);if(!v)throw std::bad_alloc();put(o,k,v);}
json_object*shape(uint32_t h,uint32_t w){auto*a=json_object_new_array();if(!a)throw std::bad_alloc();for(uint64_t n:{1u,3u,h,w}){auto*v=json_object_new_uint64(n);if(!v||json_object_array_add(a,v)){json_object_put(v);json_object_put(a);throw std::bad_alloc();}}return a;}
bool same_hw(const BackendIdentity&a,const BackendIdentity&b){return a.hardware_contract==b.hardware_contract&&a.bit==b.bit&&a.xsa==b.xsa&&a.compatible==b.compatible&&a.device_generation==b.device_generation&&a.provider_flags==b.provider_flags&&a.board_identity_verified==b.board_identity_verified;}
std::string key(const RequestKey&k){return k.boot_id+'\0'+k.client_id+'\0'+k.request_id;}
const char*numeric(vxnn::NumericClass n){switch(n){case vxnn::NumericClass::PASS:return "PASS";case vxnn::NumericClass::KNOWN_NUMERIC_DEVIATION:return "KNOWN_NUMERIC_DEVIATION";case vxnn::NumericClass::NEW_MISMATCH:return "FAIL";case vxnn::NumericClass::NOT_EVALUATED:return "NOT_EVALUATED";}throw std::runtime_error("unknown numeric class");}
const char*scope_name(vxnn::EvidenceScope s){switch(s){case vxnn::EvidenceScope::OFFLINE:return "OFFLINE";case vxnn::EvidenceScope::HOST_PROTOCOL_TEST:return "HOST_PROTOCOL_TEST";case vxnn::EvidenceScope::CROSS_COMPILE:return "CROSS_COMPILE";case vxnn::EvidenceScope::IMAGE:return "IMAGE";case vxnn::EvidenceScope::REAL_BOARD:return "REAL_BOARD";}throw std::runtime_error("unknown evidence scope");}
Digest quant(const NativeMosaicExportSpec&s){vxl_hunet_sha h;Digest out{};vxl_hunet_sha_init(&h);const char domain[]="NATIVE_MOSAIC_QUANT_BINDING_V1";vxl_hunet_sha_update(&h,domain,sizeof(domain));vxl_hunet_sha_update(&h,s.implementation.data(),32);vxl_hunet_sha_update(&h,s.model.data(),32);vxl_hunet_sha_final(&h,out.data());return out;}
}
struct NativeMosaicExporter::State {
 std::shared_ptr<HostMemoryLease>charge;std::weak_ptr<Runtime>runtime;std::shared_ptr<NativeFiles>files;BackendIdentity hardware;vxnn::EvidenceScope scope;std::string boot;
 struct Record{Digest fingerprint{};std::optional<NativeResultExport>result;std::optional<vxnn::Error>error;std::shared_ptr<NativeRequestFiles>writer;};
 std::map<Digest,NativeMosaicExportSpec>models;std::map<std::string,Record>records;std::mutex mutex;
 Result<void>validate(const PublishedResult&p,const std::string&destination)const{
  if(destination!="mosaic-file-v1")return Result<void>::failure({EINVAL,"NATIVE_EXPORT","only registered mosaic-file-v1 destination is admitted"});
  auto rt=runtime.lock();if(!rt||!files->owns_runtime(rt.get())||!same_hw(hardware,rt->identity())||scope!=rt->evidence_scope())return Result<void>::failure({ESHUTDOWN,"NATIVE_EXPORT","original shared Runtime/file owner expired"});
  auto valid=validate_published_result(p);if(!valid)return valid;
  auto layout=nn_result_artifact_layout(p);if(!layout)return layout;
  if(!p.execution_complete||p.lease.app!=AppKind::NN||!models.count(p.lease.model)||p.lease.scope!=scope||p.lease.producer.boot_id!=boot||p.lease.hardware!=hardware.hardware_contract||!p.binding)return Result<void>::failure({EXDEV,"NATIVE_EXPORT","completed same boot and admitted Mosaic source required"});
  const auto&b=*p.binding;if(b.hardware!=hardware.hardware_contract||b.bit!=hardware.bit||b.xsa!=hardware.xsa||b.compatible!=hardware.compatible||b.device_generation!=hardware.device_generation||b.provider_flags!=hardware.provider_flags)return Result<void>::failure({EXDEV,"NATIVE_EXPORT","complete source hardware identity differs"});
  const auto&a=p.lease.artifacts[0];if(a.layout!="CHW_F32"||a.shape.size()!=3||a.shape[0]!=3||!a.shape[1]||!a.shape[2]||a.shape[1]>1080||a.shape[2]>1920||a.bytes!=12*a.shape[1]*a.shape[2])return Result<void>::failure({EINVAL,"NATIVE_EXPORT","bounded original three-channel CHW FP32 source required"});
  if(p.lease.artifacts.size()>1){
   auto raw=files->read(p.lease.artifacts[1]);if(!raw)return Result<void>::failure(*raw.error);std::string input_record(raw.value->begin(),raw.value->end());auto tree=j::parse(input_record);auto*o=tree.get();auto receipt=j::parse(p.raw_legacy_json);
   if(p.lease.artifacts.size()==4)j::fields(o,{"format","kind","model","plan","tensor_sha","bytes","source_sha","source_format","preprocessing","preprocessing_id","dtype","layout","shape","input_artifact","gpu_raw_cycles","numeric"});else j::fields(o,{"format","kind","model","plan","tensor_sha","bytes","gpu_raw_cycles","numeric"});
   j::check(j::str(j::member(o,"format"))=="nn-input-record-v1"&&j::digest(j::member(o,"model"))==b.model&&j::digest(j::member(o,"plan"))==b.plan&&j::digest(j::member(o,"tensor_sha"))==b.input&&j::u64(j::member(o,"bytes"))==b.input_bytes&&!j::member(o,"gpu_raw_cycles")&&j::str(j::member(o,"numeric"))=="NOT_EVALUATED"&&j::str(j::member(receipt.get(),"input_record_json"))==input_record,"authoritative original request input record differs execution binding/receipt");
   auto inventory=files->read(p.lease.artifacts[2]);if(!inventory)return Result<void>::failure(*inventory.error);j::check(j::digest(j::member(receipt.get(),"plan_inventory_sha256"))==p.lease.artifacts[2].sha,"original plan inventory digest differs producer receipt");auto graph=j::parse(std::string(inventory.value->begin(),inventory.value->end()));j::check(j::str(j::member(graph.get(),"format"))=="nn-plan-inventory-v1"&&j::digest(j::member(graph.get(),"plan"))==b.plan,"actual plan inventory identity differs");
   const auto kind=j::str(j::member(o,"kind"));if(p.lease.artifacts.size()==4){
    j::check(kind=="UPLOADED_IMAGE_TENSOR"&&j::str(j::member(o,"dtype"))=="FP32"&&j::str(j::member(o,"layout"))=="CHW_F32"&&j::str(j::member(o,"input_artifact"))=="request-input.f32"&&j::str(j::member(o,"preprocessing"))==nn_preprocessing_policy(true)&&j::digest(j::member(o,"preprocessing_id"))==vxnn::content_digest(nn_preprocessing_policy(true))&&nonzero_digest(j::digest(j::member(o,"source_sha"))),"uploaded Mosaic input source/policy differs");
    auto format=j::str(j::member(o,"source_format"));j::check(format=="PNG"||format=="JPEG"||format=="BMP","supported original image format required");auto*shape=j::member(o,"shape");j::check(j::count(shape,3)==3,"exact CHW input shape");for(unsigned i=0;i<3;i++)j::check(j::u64(j::at(shape,i))==a.shape[i],"fixed input shape differs output geometry");auto tensor=files->read(p.lease.artifacts[3]);if(!tensor)return Result<void>::failure(*tensor.error);for(size_t i=0;i<tensor.value->size();i+=4){float value;std::memcpy(&value,tensor.value->data()+i,4);j::check(std::isfinite(value)&&value>=0&&value<=255,"actual processed RGB input range");}
   }else j::check(kind=="FROZEN_REGRESSION_INPUT","missing uploaded input tensor cannot use frozen provenance");
  }
  if(b.device_generation>UINT32_MAX)return Result<void>::failure({EOVERFLOW,"NATIVE_EXPORT","legacy file schema cannot truncate the source epoch"});
  if(scope==vxnn::EvidenceScope::REAL_BOARD&&(!p.gpu_raw_cycles||!*p.gpu_raw_cycles))return Result<void>::failure({ENODATA,"NATIVE_EXPORT","real neural source requires its original raw GPU cycles"});
  if(p.raw_legacy_json.empty())return Result<void>::failure({ENODATA,"NATIVE_EXPORT","original producer receipt must be preserved"});
  return Result<void>::success();
 }
 std::string manifest(const PublishedResult&p)const{
  const auto&b=*p.binding;const auto&a=p.lease.artifacts[0];const auto&spec=models.at(p.lease.model);const auto h=uint32_t(a.shape[1]),w=uint32_t(a.shape[2]);auto o=object();
  number(o.get(),"schema",1);text(o.get(),"run_id",vxnn::digest_hex(vxnn::content_digest(key(p.lease.producer))).substr(0,32));text(o.get(),"app","mosaic");text(o.get(),"boot_id",p.lease.producer.boot_id);number(o.get(),"generation",b.device_generation);text(o.get(),"build_id",spec.build_id);text(o.get(),"scope",scope==vxnn::EvidenceScope::REAL_BOARD?"REAL_BOARD":"OFFLINE");boolean(o.get(),"diagnostics",false);boolean(o.get(),"cycle_valid",bool(p.gpu_raw_cycles));boolean(o.get(),"timing_valid",false);if(p.gpu_raw_cycles)number(o.get(),"raw_cycles",*p.gpu_raw_cycles);else put(o.get(),"raw_cycles",nullptr);put(o.get(),"shape",shape(h,w));text(o.get(),"state","RESULT_VALIDATED");text(o.get(),"numeric_class",numeric(p.numeric));
  auto ids=object();auto bind=[&](const char*k,const Digest&d){text(ids.get(),k,vxnn::digest_hex(d));};bind("hardware",b.hardware);bind("source",b.source);bind("program",spec.program);bind("model",b.model);bind("input",b.input);bind("quant",quant(spec));const auto layout=vxnn::content_digest("NATIVE_CHW_F32_LE_LAYOUT_V1:"+std::to_string(h)+":"+std::to_string(w));bind("layout",layout);put(o.get(),"identities",json_object_get(ids.get()));
  auto asset=object();text(asset.get(),"role","output");text(asset.get(),"name","output.f32");text(asset.get(),"dtype","FP32");put(asset.get(),"shape",shape(h,w));number(asset.get(),"bytes",a.bytes);text(asset.get(),"sha256",vxnn::digest_hex(a.sha));auto*assets=json_object_new_array();if(!assets)throw std::bad_alloc();if(json_object_array_add(assets,json_object_get(asset.get()))){json_object_put(assets);throw std::bad_alloc();}put(o.get(),"artifacts",assets);
  // This additive legacy-envelope field preserves exact native provenance;
  // OFFLINE is a compatibility projection, never a promotion of HOST data.
  auto origin=object();text(origin.get(),"format","unified-native-mosaic-origin-v1");text(origin.get(),"evidence_scope",scope_name(p.lease.scope));text(origin.get(),"boot_id",p.lease.producer.boot_id);text(origin.get(),"client_id",p.lease.producer.client_id);text(origin.get(),"request_id",p.lease.producer.request_id);number(origin.get(),"device_generation",b.device_generation);number(origin.get(),"provider_flags",b.provider_flags);number(origin.get(),"input_bytes",b.input_bytes);
  for(const auto&row:std::initializer_list<std::pair<const char*,Digest>>{{"hardware",b.hardware},{"bit",b.bit},{"xsa",b.xsa},{"compatible",b.compatible},{"model",b.model},{"plan",b.plan},{"input",b.input},{"source",b.source},{"program",spec.program},{"implementation",spec.implementation},{"tensor",a.sha}})text(origin.get(),row.first,vxnn::digest_hex(row.second));
  text(origin.get(),"raw_legacy_json",p.raw_legacy_json);text(origin.get(),"raw_legacy_sha256",vxnn::digest_hex(vxnn::content_digest(p.raw_legacy_json)));text(origin.get(),"quant_binding_format","NATIVE_MOSAIC_QUANT_BINDING_V1");text(origin.get(),"layout_binding_format","NATIVE_CHW_F32_LE_LAYOUT_V1");text(origin.get(),"numeric_class",numeric(p.numeric));text(origin.get(),"build_id",spec.build_id);put(o.get(),"native_origin",json_object_get(origin.get()));
  auto raw=j::dump(o.get());j::check(raw.size()<=256u*1024u,"bounded exported metadata required");return raw;
 }
};
NativeMosaicExporter::NativeMosaicExporter(std::shared_ptr<State>s):state_(std::move(s)){}
Result<std::shared_ptr<NativeMosaicExporter>>NativeMosaicExporter::create(const StartupContext&c,std::shared_ptr<NativeFiles>files,std::vector<NativeMosaicExportSpec>models){
 using R=Result<std::shared_ptr<NativeMosaicExporter>>;try{
  auto rt=c.runtime.lock();if(!rt||!files||!files->owns_runtime(rt.get())||c.results.lock()!=rt->trusted_results()||!same_hw(c.identity,rt->identity())||!s8_provider(c.identity)||c.scope!=rt->evidence_scope()||(c.scope!=vxnn::EvidenceScope::OFFLINE&&c.scope!=vxnn::EvidenceScope::HOST_PROTOCOL_TEST&&c.scope!=vxnn::EvidenceScope::REAL_BOARD)||c.boot_id.empty()||models.empty()||models.size()>64)return R::failure({EXDEV,"NATIVE_EXPORT","same S8 Runtime/files/results and bounded trusted model set required"});
  auto s=std::make_shared<State>();s->runtime=rt;s->files=std::move(files);s->hardware=c.identity;s->scope=c.scope;s->boot=c.boot_id;
  for(auto spec:models){if(spec.build_id.empty()&&c.scope!=vxnn::EvidenceScope::REAL_BOARD)spec.build_id="UNKNOWN_NATIVE_IMAGE_BUILD";if(!nonzero_digest(spec.model)||!nonzero_digest(spec.program)||!nonzero_digest(spec.implementation)||spec.build_id.empty()||spec.build_id.size()>128||std::any_of(spec.build_id.begin(),spec.build_id.end(),[](unsigned char x){return x<32;})||!s->models.emplace(spec.model,std::move(spec)).second)return R::failure({EINVAL,"NATIVE_EXPORT","exact distinct Mosaic program/model/implementation and declared build identity required"});}
  auto charge=rt->reserve_host_memory(1u<<20);if(!charge)return R::failure(*charge.error);s->charge=*charge.value;return R::success(std::shared_ptr<NativeMosaicExporter>(new NativeMosaicExporter(std::move(s))));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"NATIVE_EXPORT","bounded exporter metadata allocation"});}
}
Result<NativeResultExport>NativeMosaicExporter::export_result(const PublishedResult&p,const std::string&destination){
 using R=Result<NativeResultExport>;auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);State::Record*record=nullptr;
 try{
  auto valid=s.validate(p,destination);if(!valid)return R::failure(*valid.error);auto raw_meta=s.manifest(p);auto fingerprint=vxnn::content_digest(raw_meta);auto k=key(p.lease.producer);auto found=s.records.find(k);
  if(found!=s.records.end()){if(found->second.fingerprint!=fingerprint)return R::failure({ESTALE,"NATIVE_EXPORT","same producer export metadata cannot change"});if(found->second.error)return R::failure(*found->second.error);if(found->second.result)return R::success(*found->second.result);return R::failure({EBUSY,"NATIVE_EXPORT","original export is incomplete"});}
  if(s.records.size()>=256){return R::failure({ENOSPC,"NATIVE_EXPORT","export metadata index256 exhausted"});}
  State::Record fresh;fresh.fingerprint=fingerprint;record=&s.records.emplace(k,std::move(fresh)).first->second;
  auto rt=s.runtime.lock();if(!rt)throw vxnn::Error{ESHUTDOWN,"NATIVE_EXPORT","common Runtime expired"};auto charge=rt->reserve_host_memory(p.lease.artifacts[0].bytes+(512u<<10));if(!charge)throw *charge.error;
  auto data=s.files->read(p.lease.artifacts[0]);if(!data)throw *data.error;for(size_t i=0;i<data.value->size();i+=4){float f;std::memcpy(&f,data.value->data()+i,4);if(!std::isfinite(f))throw vxnn::Error{EDOM,"NATIVE_EXPORT","nonfinite original output retained, not converted"};}
  const char domain[]="NATIVE_MOSAIC_FILE_EXPORT_V1";
  RequestKey derived{s.boot,"native-export",vxnn::digest_hex(vxnn::content_digest(std::string(domain,sizeof(domain))+k))};auto writer=s.files->request(derived,4096,data.value->size()+raw_meta.size());if(!writer)throw *writer.error;record->writer=*writer.value;
  auto tensor=record->writer->output("output.f32",{data.value->data(),data.value->size()},"CHW_F32",p.lease.artifacts[0].shape);if(!tensor)throw *tensor.error;
  auto metadata=record->writer->output("mosaic-result.json",{reinterpret_cast<const uint8_t*>(raw_meta.data()),raw_meta.size()},"JSON_UTF8",{raw_meta.size()});if(!metadata)throw *metadata.error;
  record->result=NativeResultExport{*metadata.value,*tensor.value};return R::success(*record->result);
 }catch(const vxnn::Error&e){if(record&&!record->error)record->error=e;return R::failure(e);}catch(const std::bad_alloc&){vxnn::Error e{ENOMEM,"NATIVE_EXPORT","export allocation; original/partial files retained"};if(record&&!record->error)record->error=e;return R::failure(std::move(e));}catch(const std::exception&e){vxnn::Error error{EINVAL,"NATIVE_EXPORT",e.what()};if(record&&!record->error)record->error=error;return R::failure(std::move(error));}
}
}
