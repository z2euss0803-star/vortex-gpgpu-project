#include <vxunified/native_nn.hpp>
#include <vxunified/native_files.hpp>
#include "scheduler_fixture.hpp"
#include "hunet_owner_bridge.h"
#include "nnrt/package/json.hpp"
#include <fstream>
#include <sstream>
#include <set>
#include <dirent.h>
#include <time.h>
namespace j=vxnn::json;
namespace {
uint64_t ns(){timespec t{};require(!clock_gettime(CLOCK_MONOTONIC,&t),"host clock");return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;}
struct Transport:IoctlTransport{void*bridge;Transport(const char*catalog,const char*code,Digest compatible):bridge(hunet_owner_bridge_create(catalog,code,compatible.data())){}~Transport(){hunet_owner_bridge_destroy(bridge);}int call(unsigned long c,void*p)override{return hunet_owner_bridge_call(bridge,c,p);}uint64_t now_ns()override{return ns();}};
void save(const std::string&path,const void*data,size_t bytes,mode_t mode=0400){int fd=open(path.c_str(),O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,mode);require(fd>=0,"fresh fixture file");size_t done=0;while(done<bytes){auto n=write(fd,static_cast<const uint8_t*>(data)+done,bytes-done);require(n>0,"whole fixture write");done+=size_t(n);}require(!fsync(fd)&&!close(fd),"fixture durable close");}
void save(const std::string&path,const std::string&bytes){save(path,bytes.data(),bytes.size());}
const uint8_t png[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,4,0,0,0,181,28,12,2,0,0,0,11,73,68,65,84,120,218,99,252,255,31,0,3,3,2,0,239,191,107,223,0,0,0,0,73,69,78,68,174,66,96,130};
Digest hash(const void*p,size_t n){Digest d{};vxl_hunet_hash(p,n,d.data());return d;}
std::string id(unsigned n){std::ostringstream s;s<<"example-roi_"<<n;return s.str();}
struct Preset {
 std::string directory;Digest manifest{},tensor{},source{};std::vector<uint8_t>bytes;
 Preset(const std::string&parent,unsigned count=2,unsigned pages=1,const std::function<void(json_object*)>&mutate={}):bytes(196608){
  static unsigned serial=0;directory=parent+"/fixed-"+std::to_string(++serial);require(!mkdir(directory.c_str(),0700),"private fixture preset dir");
  source=hash(png,sizeof(png));for(size_t i=0;i<bytes.size();i+=4){float x=.5f;std::memcpy(bytes.data()+i,&x,4);}tensor=hash(bytes.data(),bytes.size());
  std::ostringstream s;s<<"{\"format\":\"hunet-fixed-inputs-v1\",\"version\":1,\"source_manifest_sha256\":\""<<vxnn::digest_hex(digest(61))<<"\",\"preprocessing\":\"HUNET_REVIEWED_P0_FROZEN_V1\",\"preprocessing_id\":\"3c630630d94b6edd8cdc8f7b592ddca88b1e548270f42cb13d460881b09cfc71\",\"converter_sha256\":\"ebebc5c9402d1414faeb1abc5573b35a1e66276439864fef01b85534d13d51f2\",\"dtype\":\"FP32\",\"layout\":\"CHW_F32\",\"shape\":[3,128,128],\"pages\":[";
  unsigned number=0;for(unsigned page=0;page<pages;page++){
   if(page)s<<',';s<<"{\"id\":\"page"<<page+1<<"\",\"title\":\"reviewed page\",\"source_sha256\":\""<<vxnn::digest_hex(digest(uint8_t(71+page)))<<"\",\"items\":[";
   unsigned in_page=count/pages+(page<count%pages);for(unsigned n=0;n<in_page;n++,number++){
    if(n)s<<',';auto input=id(number);s<<"{\"input_id\":\""<<input<<"\",\"roi_id\":\"roi_"<<number<<"\",\"bbox_xyxy\":[1,2,7,9],\"source_file\":\""<<input<<".source\",\"source_format\":\"PNG\",\"source_bytes\":"<<sizeof(png)<<",\"source_sha256\":\""<<vxnn::digest_hex(source)<<"\",\"tensor_file\":\""<<input<<".f32\",\"tensor_bytes\":196608,\"tensor_sha256\":\""<<vxnn::digest_hex(tensor)<<"\"}";
    save(directory+"/"+input+".source",png,sizeof(png));save(directory+"/"+input+".f32",bytes.data(),bytes.size());
   }s<<"]}";
  }s<<"]}";auto tree=j::parse(s.str());if(mutate)mutate(tree.get());const auto raw=j::dump(tree.get());manifest=vxnn::content_digest(raw);save(directory+"/manifest.json",raw);save(directory+"/manifest.sha256",vxnn::digest_hex(manifest)+"\n");require(!chmod(directory.c_str(),0500),"seal preset directory0500");
 }
};
json_object*item(json_object*o){return j::at(j::member(j::at(j::member(o,"pages"),0),"items"),0);}
}
int main(int argc,char**argv){
 require(argc==5,"copied actual package/catalog/code/mode");require(geteuid()==0,"fixed preset tests use root-owned temporary fixtures");const std::string mode=argv[4];
 auto package=value(vxnn::load_package(vxnn::Path(argv[1])),"frozen HUNet package");const auto&binding=*package.assets().at("binding.json").content;auto doc=j::parse(std::string(binding.begin(),binding.end()));auto*c=doc.get();vxnn::HunetFamily family{j::digest(j::member(c,"code_sha256")),j::digest(j::member(c,"implementation_sha256")),j::digest(j::member(c,"schema_sha256"))};auto compatible=vxnn::content_digest(j::str(j::member(j::member(c,"hardware"),"compatible")));
 char dir[]="/tmp/vx-hunet-presets-332-XXXXXX";require(mkdtemp(dir),"root-owned bounded fixture root");ServiceConfig config;config.private_dir=dir;unsigned opens=0;auto owner=value(DeviceOwner::acquire(config,[&](const std::string&){++opens;return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"one fake HOST owner");std::shared_ptr<NativeRouter>router;Transport*io=nullptr;
 ExecutionHooks route;route.prepare_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return router->handlers().prepare_gpu(w,b,r);};route.finish_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return router->handlers().finish_gpu(w,b,r);};route.non_gpu=[&](const WorkItem&w,UnifiedBackend&b,vxnn::CpuPool*p,ExecutionReceipt&r){return router->handlers().non_gpu(w,b,p,r);};route.retire_request=[&](RequestKey k,UnifiedBackend&b){return router->handlers().retire_request(k,b);};
 auto port=std::make_shared<ComputeExecutionPort>(owner,route,[&](int){auto t=std::make_unique<Transport>(argv[2],argv[3],compatible);io=t.get();return t;});auto hw=value(port->probe(owner->fd(),BackendMode::COMPUTE_V2),"actual C fixture capabilities");auto backend=value(port->adapter_backend(),"one backend");auto caps=value(backend->capabilities(),"frozen address domains");std::ifstream f(argv[2],std::ios::binary);std::vector<uint8_t>raw((std::istreambuf_iterator<char>(f)),{});auto catalog=value(InstalledCatalog::decode({raw.data(),raw.size()},hw,hash(raw.data(),raw.size())),"original installed catalog");auto residency=value(ProgramResidency::create(catalog,backend),"one program residency");
 auto rt=value(Runtime::start(owner,hw,vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[](const ExecutionReceipt&,DeliveryStage){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"common HOST Runtime");StartupContext root;root.borrowed_fd=owner->fd();root.identity=hw;root.compute_backend=backend;root.runtime=rt;root.results=rt->trusted_results();root.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;root.boot_id="boot";root.private_dir=dir;router=value(NativeRouter::create(root),"same common router");std::string output=std::string(dir)+"/outputs";require(!mkdir(output.c_str(),0700),"private outputs");auto files=value(NativeFiles::create(root,output),"same output owner");
 vxnn::OperatorRegistry registry;ok(vxnn::register_hunet_operators(registry,family),"original operators");auto base=value(vxnn::prepare_hunet(package,registry,caps,hw),"original actual plan");InstalledNnModel spec;spec.id="hunet-native";spec.input_id="frozen";spec.package_directory=argv[1];spec.model=package.model_id();spec.plan=base.plan_id;spec.input=package.assets().at("input.f32").sha;spec.input_bytes=196608;spec.resident_bytes=package.resident_bytes();spec.family=family;spec.job_budget_ns=60000000000ULL;
 auto library=[&](){return value(NativeNnLibrary::create(root,router,residency,files),"real native library");};auto declaration=[&](const Preset&p){auto copy=spec;copy.presets=InstalledNnPresets{p.directory,p.manifest};return copy;};
 Preset fixed(dir,128,8);auto lib=library();const auto before=rt->stats().cpu_bytes;auto entry=value(lib->entry(declaration(fixed)),"fixed preset metadata admission");auto listed=value(entry.list_inputs(),"metadata standard input listing");require(listed.size()==129,"fixed128 entries must be listed outside dynamic8 registry");require(rt->stats().cpu_bytes==before+(8u<<20)&&rt->stats().resident_models==0,"explicit8MiB metadata/base-plan lease with no pinned model or128-tensor cache");
#ifdef VX_PRESETS_RED_ONLY
 return 0;
#endif
 for(unsigned i=0;i<128;i++){const auto&r=listed[i+1];require(r.id==id(i)&&r.sha==fixed.tensor&&r.bytes==196608&&r.plan!=base.plan_id,"fixed manifest order/identity/expected new plan");}
 RpcRequest q;q.command="submit";q.request={"boot","presets","first"};q.entry_id=spec.id;q.input_id=id(0);q.parameters="{}";
 auto request=value(entry.prepare(q),"selected fixed input reads/verifies and binds original graph");require(request.input_sha==fixed.tensor&&request.plan==listed[1].plan&&request.model==spec.model,"selected payload/binder matches listed expected identity");require(!hunet_owner_bridge_starts(io->bridge),"metadata/selection performs no GPU START");
 if(mode=="unknown"){
  hunet_owner_bridge_corrupt_guard(io->bridge);value(rt->submit(request),"same original queued source");require(!rt->run_one(),"original guard failure retained");const auto held=rt->stats().cpu_bytes;require(!lib->shutdown()&&rt->stats().cpu_bytes==held&&!rt->shutdown(),"unknown retirement cannot release fixed metadata/model/tensor");std::puts("R5_HUNET_PRESETS_UNKNOWN_RETAINED_HOST_NOT_GPU");return 0;
 }
 require(!lib->shutdown(),"cache cannot close before Runtime retirement");request.source.reset();require(router->indexed()==0,"unadmitted metadata source releases normally");
 // Unselected payload corruption is not read during metadata admission/list;
 // it must fail when that exact fixed ID is selected.
 std::string bad_path=fixed.directory+"/"+id(127)+".f32";require(!chmod(bad_path.c_str(),0600),"owned fixture mutation");int changed=open(bad_path.c_str(),O_WRONLY|O_CLOEXEC);require(changed>=0,"mutated fixture open");uint8_t byte=17;require(pwrite(changed,&byte,1,0)==1&&!close(changed)&&!chmod(bad_path.c_str(),0400),"sealed changed unselected byte");require(value(entry.list_inputs(),"listing remains metadata only").size()==129,"listing does not cache or read all fixed tensors");q.input_id=id(127);q.request.request_id="bad-payload";require(!entry.prepare(q)&&!hunet_owner_bridge_starts(io->bridge),"selected tampered tensor rejected before START");q.input_id=id(0);q.request.request_id="next-good";auto next=value(entry.prepare(q),"valid selected neighbor remains independent");require(next.plan==listed[1].plan,"same selected plan stable");next.source.reset();
 std::string inbox=std::string(dir)+"/input-inbox";ok(lib->set_input_directory(inbox),"old dynamic inbox");NnInputRecord input;input.source_sha=fixed.source;input.tensor_sha=fixed.tensor;input.shape={3,128,128};input.source_format="PNG";input.preprocessing=nn_preprocessing_policy(false);input.preprocessing_id=vxnn::content_digest(input.preprocessing);
 input.id="example-custom";save(inbox+"/"+input.id+".source",png,sizeof(png));save(inbox+"/"+input.id+".f32",fixed.bytes.data(),fixed.bytes.size());require(!lib->register_input(spec.id,input),"reviewed example namespace reserved");input.id=id(0);require(!lib->register_input(spec.id,input),"dynamic cannot shadow a fixed ID");for(unsigned i=0;i<9;i++){input.id="dynamic-"+std::to_string(i);save(inbox+"/"+input.id+".source",png,sizeof(png));save(inbox+"/"+input.id+".f32",fixed.bytes.data(),fixed.bytes.size());auto admitted=lib->register_input(spec.id,input);require(i<8?bool(admitted):(!admitted&&admitted.error->code==ENOSPC),"dynamic8 quota unaffected by128 fixed metadata rows");}
 require(value(entry.list_inputs(),"fixed and dynamic listing").size()==137,"base plus128 fixed plus8 dynamic");
 auto rejected=[&](Preset&p,const char*why){auto other=library();const auto held=rt->stats().cpu_bytes;require(!other->entry(declaration(p)),why);require(rt->stats().cpu_bytes==held,"failed preset admission rolls back host/model charges");};
 {Preset p(dir,1,1,[](json_object*o){json_object_object_add(o,"preprocessing",json_object_new_string("HUNET_P0_RGB_OTSU_AREA_PAD128_F32_V1"));json_object_object_add(o,"preprocessing_id",json_object_new_string("06e7c657d57e004645eaa966c9bd9b422d3fd707bb525c30f5497c6c31820ae0"));});rejected(p,"dynamic preprocessing cannot claim frozen provenance");}
 {Preset p(dir,129,8);rejected(p,"more than128 fixed crops rejected");}
 {Preset p(dir,9,9);rejected(p,"more than8 pages rejected");}
 {Preset p(dir,1,1,[](json_object*o){json_object_object_add(item(o),"tensor_file",json_object_new_string("../escape"));});rejected(p,"path traversal rejected");}
 {Preset p(dir,1,1,[](json_object*o){json_object_object_add(item(o),"input_id",json_object_new_string("dynamic"));});rejected(p,"reserved example ID must match ROI");}
 {Preset p(dir,1,1,[](json_object*o){json_object_object_add(item(o),"tensor_bytes",json_object_new_int(4));});rejected(p,"exact FP32 extent required");}
 {Preset p(dir,1,1,[](json_object*o){json_object_object_add(o,"preprocessing_id",json_object_new_string(std::string(64,'1').c_str()));});rejected(p,"frozen preprocessing identity rejected");}
 {Preset p(dir);require(!chmod(p.directory.c_str(),0700),"writable fixture dir");rejected(p,"preset dir must be0500");}
 {Preset p(dir);require(!chmod((p.directory+"/"+id(0)+".source").c_str(),0600),"writable fixture source");rejected(p,"preset files must be0400");}
 {Preset p(dir);require(!chown((p.directory+"/"+id(0)+".source").c_str(),1,1),"foreign fixture owner");rejected(p,"root owner required");}
 {Preset p(dir);require(!chmod(p.directory.c_str(),0700),"fixture directory open");save(p.directory+"/extra",std::string("unlisted"));require(!chmod(p.directory.c_str(),0500),"restore fixture directory seal");rejected(p,"unlisted file rejected");}
 {Preset p(dir);require(!chmod(p.directory.c_str(),0700),"fixture directory open");require(!link((p.directory+"/"+id(0)+".f32").c_str(),(std::string(dir)+"/hardlink").c_str()),"fixture hardlink");require(!chmod(p.directory.c_str(),0500),"restore fixture directory seal");rejected(p,"multiple links rejected");}
 {Preset p(dir);require(!chmod(p.directory.c_str(),0700)&&!unlink((p.directory+"/"+id(0)+".f32").c_str())&&!symlink("manifest.json",(p.directory+"/"+id(0)+".f32").c_str())&&!chmod(p.directory.c_str(),0500),"fixture symlink");rejected(p,"symlink rejected");}
 {Preset p(dir);auto other=library();auto declaration_spec=declaration(p);declaration_spec.presets->manifest[0]^=1;require(!other->entry(declaration_spec),"installed manifest SHA mismatch");}
 {auto other=library();const uint64_t free=(1ULL<<30)-rt->stats().cpu_bytes;auto hog=value(rt->reserve_host_memory(free-(8u<<20)+1),"bounded reserve exhaustion fixture");const auto held=rt->stats().cpu_bytes;auto failed=other->entry(declaration(fixed));require(!failed&&failed.error->code==ENOSPC&&rt->stats().cpu_bytes==held,"metadata reserve refused before allocating/reading");}
 ok(rt->shutdown(),"normal common Runtime close");ok(lib->shutdown(),"fixed cache close after normal source retirement");require(rt->stats().resident_models==0,"closed fixed/dynamic owners release model charge");ok(residency->shutdown(),"normal program retirement");require(opens==1&&!hunet_owner_bridge_starts(io->bridge)&&!hunet_owner_bridge_live(io->bridge),"one fake owner and no GPU execution");std::puts("R5_HUNET_PRESETS_NATIVE_LAZY128_DYNAMIC8_SEALS_PLAN_QUOTA_LIFETIME_PASS_HOST_NOT_GPU");
}
