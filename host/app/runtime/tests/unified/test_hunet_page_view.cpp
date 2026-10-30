#include <vxunified/native_result_view.hpp>
#include <vxunified/hunet_page_identity.hpp>
#include "nnrt/package/json.hpp"
#include "scheduler_fixture.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <limits>
namespace j=vxnn::json;
namespace {
unsigned checks=0,cases=0;
void check(bool b,const char*why){++checks;require(b,why);}
std::vector<uint8_t> bytes(const std::string&p){std::ifstream f(p,std::ios::binary);require(bool(f),"HOST fixture file readable");return {std::istreambuf_iterator<char>(f),{}};}
std::string text(const std::string&p){auto b=bytes(p);return {b.begin(),b.end()};}
j::Ptr parse_index(const std::string&s){auto*t=json_tokener_new_ex(32);require(t,"fixture JSON allocation");json_tokener_set_flags(t,JSON_TOKENER_STRICT|JSON_TOKENER_VALIDATE_UTF8);auto*v=json_tokener_parse_ex(t,s.c_str(),int(s.size()));bool valid=json_tokener_get_error(t)==json_tokener_success;json_tokener_free(t);require(valid&&v,"fixture finite-float JSON parses");return j::Ptr(v,json_object_put);}
Digest hex(const std::string&s){return j::digest(j::parse("\""+s+"\"").get());}
BackendIdentity hw(){BackendIdentity h;h.hardware_contract=hex("1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d");h.bit=hex("94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1");h.xsa=hex("02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94");h.compatible=hex("f670c653a37361f05f57c14eecd53dea1b87556489a2c1314efd208d715826dc");h.device_generation=1;h.provider_flags=4;return h;}
struct Port:ExecutionPort{unsigned runs=0;Result<BackendIdentity>probe(int,BackendMode)override{return Result<BackendIdentity>::success(hw());}Result<void>execute(int,const WorkItem&,vxnn::CpuPool*,ExecutionReceipt&)override{++runs;return Result<void>::failure({EPERM,"PAGE520","HOST view cannot execute GPU"});}Result<void>retire_request(int,RequestKey)override{return Result<void>::success();}};
void field(json_object*o,const char*k,const std::string&v){json_object_object_add(o,k,json_object_new_string_len(v.data(),int(v.size())));}
void integer(json_object*o,const char*k,uint64_t v){json_object_object_add(o,k,json_object_new_uint64(v));}
}
int main(int argc,char**argv){
 require(argc==6,"view dir/SHA, fixture dir, private RAM owner, evidence dir required");
 const std::string fixture=argv[3],root=argv[4],evidence=argv[5];
 ServiceConfig cfg;cfg.private_dir=root;auto owner=value(DeviceOwner::acquire(cfg,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"HOST sole owner");auto port=std::make_shared<Port>();auto rt=value(Runtime::start(owner,hw(),vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[](const ExecutionReceipt&,DeliveryStage){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"HOST common runtime");StartupContext c;c.runtime=rt;c.results=rt->trusted_results();c.identity=hw();c.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;c.boot_id="boot";c.private_dir=root;
 check(!mkdir((root+"/outputs").c_str(),0700),"private outputs created");auto files=value(NativeFiles::create(c,root+"/outputs"),"real immutable NativeFiles");auto meta=parse_index(text(fixture+"/page-result.json"));auto*o=meta.get();const auto model=j::digest(j::member(o,"model")),base=j::digest(j::member(o,"base_plan"));
 check(nonzero_digest(base),"fixture base identity nonzero");
 NativeHunetViewSpec spec;spec.directory=argv[1];spec.manifest=hex(argv[2]);spec.models={model};
#ifdef VXUNIFIED_HUNET_PAGE_VIEW_V1
 spec.page_presets.push_back({fixture,j::digest(j::member(o,"catalog_sha256")),model,base});
#endif
 auto view=value(NativeResultView::create(c,files,spec),"strict trusted page view creation");
 auto logits=bytes(fixture+"/page-logits.f32"),rgb=bytes(fixture+"/page-source.rgb"),original=bytes(fixture+"/page-original.source");const auto n=j::u32(j::member(o,"total")),w=j::u32(j::member(o,"width")),h=j::u32(j::member(o,"height"));
 unsigned sequence=0;
 auto make=[&](const std::string&mode)->PublishedResult{
  auto index=parse_index(text(fixture+"/page-result.json"));auto*obj=index.get();auto*rows=j::member(obj,"words");auto*first=j::at(rows,0);auto raw=logits,source=rgb,orig=original;
  if(mode=="wrong-index")integer(first,"index",1);
  if(mode=="wrong-roi")field(first,"roi_id","foreign-roi");
  if(mode=="wrong-input-id")field(first,"input_id","example-foreign");
  if(mode=="wrong-crop")field(first,"source_sha256",vxnn::digest_hex(digest(91)));
  if(mode=="wrong-tensor")field(first,"tensor_sha256",vxnn::digest_hex(digest(92)));
  if(mode=="wrong-policy")field(first,"preprocessing","UNDECLARED_POLICY");
  if(mode=="wrong-policy-id")field(first,"preprocessing_id",vxnn::digest_hex(digest(93)));
  if(mode=="wrong-word-plan")field(first,"plan",vxnn::digest_hex(digest(94)));
  if(mode=="wrong-offset")integer(first,"offset",4);
  if(mode=="wrong-word-bytes")integer(first,"bytes",32416);
  if(mode=="wrong-word-logits-sha")field(first,"logits_sha256",vxnn::digest_hex(digest(95)));
  if(mode=="wrong-word-trace")field(first,"durable_record","trace.raw#word-1");
  if(mode=="wrong-top-id")json_object_array_put_idx(j::member(first,"topk_class_ids"),0,json_object_new_int(0));
  if(mode=="integer-score-alias"){float high=std::ldexp(1.f,64);std::memcpy(raw.data()+7*4,&high,4);field(first,"logits_sha256",vxnn::digest_hex(vxnn::content_digest(std::string(raw.begin(),raw.begin()+32420))));json_object_array_put_idx(j::member(first,"topk_raw_logits"),0,json_object_new_uint64(UINT64_MAX));}
  if(mode=="rounded-score")json_object_array_put_idx(j::member(first,"topk_raw_logits"),0,json_object_new_double(12.500000001));
  if(mode=="wrong-top-score")json_object_array_put_idx(j::member(first,"topk_raw_logits"),0,json_object_new_double(12.25));
  if(mode=="wrong-bbox")json_object_array_put_idx(j::member(first,"bbox_xyxy"),0,json_object_new_int(1));
  if(mode=="wrong-order")field(obj,"order","SORTED_GUESSED_ORDER");
  if(mode=="wrong-score-kind")field(obj,"score_kind","PROBABILITY");
  if(mode=="wrong-numeric")field(obj,"numeric_class","PASS");
  if(mode=="wrong-catalog")field(obj,"catalog_sha256",vxnn::digest_hex(digest(96)));
  if(mode=="wrong-page")field(obj,"page_id","page-foreign");
  if(mode=="wrong-title")field(obj,"title","foreign title");
  if(mode=="wrong-completed")integer(obj,"completed",n-1);
  if(mode=="wrong-total")integer(obj,"total",n-1);
  if(mode=="wrong-source-sha")field(obj,"source_sha256",vxnn::digest_hex(digest(97)));
  if(mode=="wrong-rgb-sha")field(obj,"rgb_sha256",vxnn::digest_hex(digest(98)));
  if(mode=="wrong-width")integer(obj,"width",w+1);
  if(mode=="unknown-field")integer(obj,"peer_claim",1);
  if(mode=="swapped-word"&&n>1){auto*a=json_object_get(j::at(rows,0)),*b=json_object_get(j::at(rows,1));json_object_array_put_idx(rows,0,b);json_object_array_put_idx(rows,1,a);}
  if(mode=="nan"||mode=="infinity"){uint32_t bits=mode=="nan"?0x7fc12345u:0x7f800000u;std::memcpy(raw.data()+400,&bits,4);field(first,"logits_sha256",vxnn::digest_hex(vxnn::content_digest(std::string(raw.begin(),raw.begin()+32420))));}
  if(mode=="nan-last"){uint32_t bits=0x7fc12345u;const size_t last=(n-1)*32420;std::memcpy(raw.data()+last+400,&bits,4);field(j::at(rows,n-1),"logits_sha256",vxnn::digest_hex(vxnn::content_digest(std::string(raw.begin()+last,raw.begin()+last+32420))));}
  if(mode=="rgb-changed")source[source.size()/2]^=1;
  if(mode=="original-changed")orig[0]^=1;
  if(mode=="optional-times"){integer(first,"begin_ns",10);integer(first,"end_ns",20);}
  if(mode=="optional-performance"){auto*perf=json_object_new_object();for(auto k:{"constant_uploads","constant_upload_bytes","input_uploads","input_upload_bytes","data_allocations","args_allocations","words_completed"})integer(perf,k,1);json_object_object_add(perf,"gpu_raw_cycles",nullptr);json_object_object_add(obj,"performance",perf);}
  if(mode=="valid-negative"||mode=="valid-tie"||mode=="valid-negative-zero"||mode=="wrong-zero-sign"){
   std::vector<float>values(8105,mode=="valid-negative"?-10.f:-1.f);std::array<uint32_t,5>rank{7,2,1,3,0};std::array<float,5>scores{-1.f,-2.f,-3.f,-4.f,-5.f};
   if(mode=="valid-tie"){rank={2,7,1,3,0};scores={12.5f,12.5f,9.f,8.f,7.f};}
   if(mode=="valid-negative-zero"||mode=="wrong-zero-sign"){rank={2,7,1,3,0};scores={-0.f,0.f,-.1f,-.2f,-.3f};}
   for(size_t k=0;k<5;k++){values[rank[k]]=scores[k];}
   for(size_t i=0;i<n;i++){std::memcpy(raw.data()+i*32420,values.data(),32420);auto*row=j::at(rows,i);field(row,"logits_sha256",vxnn::digest_hex(vxnn::content_digest(std::string(raw.begin()+i*32420,raw.begin()+(i+1)*32420))));auto*ids=json_object_new_array();auto*score=json_object_new_array();for(size_t k=0;k<5;k++){json_object_array_add(ids,json_object_new_int(rank[k]));json_object_array_add(score,json_object_new_double(scores[k]));}json_object_object_add(row,"topk_class_ids",ids);json_object_object_add(row,"topk_raw_logits",score);}
   if(mode=="wrong-zero-sign"){json_object_array_put_idx(j::member(j::at(rows,0),"topk_raw_logits"),0,json_object_new_double(0.0));}
  }
  const auto json=j::dump(obj);std::ofstream(evidence+"/"+mode+".json")<<json<<"\n";
  PublishedResult p;p.execution_complete=true;p.lease.producer={"boot","page520",mode+std::to_string(++sequence)};p.lease.app=AppKind::NN;p.lease.scope=c.scope;p.lease.hardware=c.identity.hardware_contract;p.lease.model=model;p.lease.plan=j::digest(j::member(o,"plan"));p.raw_legacy_json="{\"format\":\"native-hunet-page-result-v1\",\"scope\":\"HOST_PROTOCOL_TEST\"}";
  ExecutionBinding bind;bind.app=AppKind::NN;bind.hardware=c.identity.hardware_contract;bind.bit=c.identity.bit;bind.xsa=c.identity.xsa;bind.compatible=c.identity.compatible;bind.model=model;bind.plan=p.lease.plan;bind.input=j::digest(j::member(o,"input_sha256"));bind.source=digest(22);bind.input_bytes=uint64_t(n)*196608;bind.device_generation=1;bind.provider_flags=4;p.binding=bind;
  auto writer=value(files->request(p.lease.producer,4096,raw.size()+json.size()+source.size()+orig.size()+4096),"real request storage");
  p.lease.artifacts.push_back(value(writer->output("page-logits.f32",{raw.data(),raw.size()},"HUNET_PAGE_F32",{n,8105,1}),"real page logits"));p.lease.artifacts.push_back(value(writer->output("page-result.json",{reinterpret_cast<const uint8_t*>(json.data()),json.size()},"JSON_UTF8",{json.size()}),"real final index"));p.lease.artifacts.push_back(value(writer->output("page-source.rgb",{source.data(),source.size()},"RGB888",{h,w,3}),"real source RGB"));p.lease.artifacts.push_back(value(writer->output("page-original.source",{orig.data(),orig.size()},"OPAQUE_U8",{orig.size()}),"real opaque original"));if(mode=="optional-inventory"){const std::string extra="{}";p.lease.artifacts.push_back(value(writer->output("plan-inventory.json",{reinterpret_cast<const uint8_t*>(extra.data()),extra.size()},"JSON_UTF8",{extra.size()}),"optional retained plan inventory"));}return p;
 };
 auto p=make("normal");auto lr=view->layout(p,1280,720);if(!lr)std::fprintf(stderr,"PAGE520_MISSING_FINAL_PAGE code=%d category=%s detail=%s\n",lr.error->code,lr.error->category.c_str(),lr.error->detail.c_str());check(bool(lr),"final complete page must select dedicated page layout");auto layout=*lr.value;check(layout.source_width==w&&layout.source_height==h&&layout.viewport_width==960,"actual full page dimensions/left3quarters");
 const auto before=rt->stats().cpu_bytes;auto canvas=value(view->compose(p,layout),"strict final page composed");++cases;std::ofstream ppm(evidence+"/canvas.ppm",std::ios::binary);ppm<<"P6\n1280 720\n255\n";ppm.write(reinterpret_cast<const char*>(canvas->rgb.data()),canvas->rgb.size());ppm.close();
 check(canvas->rgb.size()==1280u*720*3&&canvas->source_artifact==p.lease.artifacts[0].sha,"retained canvas binds actual logits bytes");check(rt->stats().cpu_bytes>=before+canvas->rgb.size(),"canvas lifetime charged to common Runtime");
 auto again=value(view->compose(p,layout),"stable immutable repeat");check(canvas->pixel_sha==again->pixel_sha&&canvas->rgb==again->rgb,"same source/index/logits produce stable pixels");again.reset();canvas.reset();check(rt->stats().cpu_bytes==before,"last immutable canvas reference retires charge");
 auto small=value(view->layout(p,640,480),"minimum canvas with full page source");auto tiny=value(view->compose(p,small),"all words fit minimum canvas");std::ofstream ppm_small(evidence+"/canvas-small.ppm",std::ios::binary);ppm_small<<"P6\n640 480\n255\n";ppm_small.write(reinterpret_cast<const char*>(tiny->rgb.data()),tiny->rgb.size());ppm_small.close();tiny.reset();++cases;
 for(const auto*mode:{"wrong-index","wrong-roi","wrong-input-id","wrong-crop","wrong-tensor","wrong-policy","wrong-policy-id","wrong-word-plan","wrong-offset","wrong-word-bytes","wrong-word-logits-sha","wrong-word-trace","wrong-top-id","wrong-top-score","rounded-score","integer-score-alias","wrong-bbox","wrong-order","wrong-score-kind","wrong-numeric","wrong-catalog","wrong-page","wrong-title","wrong-completed","wrong-total","wrong-source-sha","wrong-rgb-sha","wrong-width","unknown-field","swapped-word","nan","infinity","nan-last","wrong-zero-sign","rgb-changed","original-changed"}){
  auto bad=make(mode);const auto budget=rt->stats().cpu_bytes;auto r=view->compose(bad,layout);if(r)std::fprintf(stderr,"PAGE520_ACCEPTED_BAD %s\n",mode);check(!r,mode);check(rt->stats().cpu_bytes==budget,"invalid page cannot retain partial canvas/charge");++cases;
 }
 for(const auto*mode:{"optional-times","optional-performance","valid-negative","valid-tie","valid-negative-zero","optional-inventory"}){auto extra=make(mode);check(bool(view->compose(extra,layout)),"allowed producer timing metadata cannot alter acceptance or scope");++cases;}
 auto bad=p;bad.lease.producer.request_id="borrowed";check(!view->compose(bad,layout),"same bytes cannot impersonate another request");++cases;
 auto other=make("other");for(size_t a=0;a<4;a++){bad=p;bad.lease.artifacts[a]=other.lease.artifacts[a];check(!view->compose(bad,layout),"another request artifact with identical SHA cannot lend source/index/logits");++cases;}
 bad=p;bad.binding->input[0]^=1;check(!view->compose(bad,layout),"aggregate input must use pure page identity");++cases;
 bad=p;bad.binding->input_bytes-=196608;check(!view->compose(bad,layout),"page input byte count must include every word");++cases;
 bad=p;bad.binding->plan[0]^=1;bad.lease.plan=bad.binding->plan;check(!view->compose(bad,layout),"page plan bound to base and ordered word metadata");++cases;
 bad=p;bad.execution_complete=false;check(!view->compose(bad,layout),"partial page never becomes final complete");++cases;
 bad=p;bad.numeric=vxnn::NumericClass::PASS;check(!view->compose(bad,layout),"page view cannot invent numeric evaluation");++cases;
 bad=p;bad.lease.artifacts.pop_back();check(!view->compose(bad,layout),"all four actual artifacts required");++cases;
 bad=p;bad.lease.artifacts[0].sha[0]^=1;check(!view->compose(bad,layout),"actual output SHA rechecked by NativeFiles");++cases;
 auto altered=layout;altered.source_height--;check(!view->compose(p,altered),"caller cannot change page source geometry");++cases;
#ifdef VXUNIFIED_HUNET_PAGE_VIEW_V1
 auto spec_bad=spec;spec_bad.page_presets[0].manifest[0]^=1;const auto budget=rt->stats().cpu_bytes;check(!NativeResultView::create(c,files,spec_bad)&&rt->stats().cpu_bytes==budget,"startup catalog pin mismatch releases all charges");++cases;
#endif
 #ifdef VXUNIFIED_HUNET_PAGE_VIEW_V1
 auto legacy_tree=j::parse(text(fixture+"/manifest.json"));field(legacy_tree.get(),"format","hunet-fixed-inputs-v1");integer(legacy_tree.get(),"version",1);auto*legacy_pages=j::member(legacy_tree.get(),"pages");for(size_t i=0;i<j::count(legacy_pages,8);i++){json_object_object_del(j::at(legacy_pages,i),"image");}const auto legacy_raw=j::dump(legacy_tree.get()),legacy_dir=root+"/legacy-catalog";check(!mkdir(legacy_dir.c_str(),0700),"new legacy catalog fixture");std::ofstream(legacy_dir+"/manifest.json")<<legacy_raw;std::ofstream(legacy_dir+"/manifest.sha256")<<vxnn::digest_hex(vxnn::content_digest(legacy_raw))<<"\n";check(!chmod((legacy_dir+"/manifest.json").c_str(),0400)&&!chmod((legacy_dir+"/manifest.sha256").c_str(),0400),"legacy catalog exact immutable permissions");auto legacy_spec=spec;legacy_spec.page_presets[0].directory=legacy_dir;legacy_spec.page_presets[0].manifest=vxnn::content_digest(legacy_raw);auto legacy_view=value(NativeResultView::create(c,files,legacy_spec),"v1 presets retain legacy admission");check(!legacy_view->layout(p,1280,720),"v1 presets add no full page entry");++cases;
#endif
 check(!port->runs&&!p.gpu_raw_cycles&&p.numeric==vxnn::NumericClass::NOT_EVALUATED&&p.lease.scope==vxnn::EvidenceScope::HOST_PROTOCOL_TEST,"HOST compositor never dispatches/upgrades scope or numerics");ok(rt->shutdown(),"HOST empty runtime shutdown");std::printf("PAGE520_PASS HOST_PROTOCOL_TEST cases=%u checks=%u words=%u source=%ux%u cycles_valid=0 GPU_NOT_RUN DP_NOT_RUN\n",cases,checks,n,w,h);
}
