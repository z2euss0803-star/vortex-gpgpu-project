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
   if(page){s<<',';}s<<"{\"id\":\"page"<<page+1<<"\",\"title\":\"reviewed page\",\"source_sha256\":\""<<vxnn::digest_hex(digest(uint8_t(71+page)))<<"\",\"items\":[";
   unsigned in_page=count/pages+(page<count%pages);for(unsigned n=0;n<in_page;n++,number++){
    if(n){s<<',';}auto input=id(number);s<<"{\"input_id\":\""<<input<<"\",\"roi_id\":\"roi_"<<number<<"\",\"bbox_xyxy\":[1,2,7,9],\"source_file\":\""<<input<<".source\",\"source_format\":\"PNG\",\"source_bytes\":"<<sizeof(png)<<",\"source_sha256\":\""<<vxnn::digest_hex(source)<<"\",\"tensor_file\":\""<<input<<".f32\",\"tensor_bytes\":196608,\"tensor_sha256\":\""<<vxnn::digest_hex(tensor)<<"\"}";
    save(directory+"/"+input+".source",png,sizeof(png));save(directory+"/"+input+".f32",bytes.data(),bytes.size());
   }s<<"]}";
  }s<<"]}";auto tree=j::parse(s.str());if(mutate)mutate(tree.get());const auto raw=j::dump(tree.get());manifest=vxnn::content_digest(raw);save(directory+"/manifest.json",raw);save(directory+"/manifest.sha256",vxnn::digest_hex(manifest)+"\n");require(!chmod(directory.c_str(),0500),"seal preset directory0500");
 }
};
}
int main(int argc,char**argv){
 require(argc==7,"copied actual package/catalog/code/mode");require(geteuid()==0,"fixed preset tests use root-owned temporary fixtures");const std::string mode=argv[4];
 auto package=value(vxnn::load_package(vxnn::Path(argv[1])),"frozen HUNet package");const auto&binding=*package.assets().at("binding.json").content;auto doc=j::parse(std::string(binding.begin(),binding.end()));auto*c=doc.get();vxnn::HunetFamily family{j::digest(j::member(c,"code_sha256")),j::digest(j::member(c,"implementation_sha256")),j::digest(j::member(c,"schema_sha256"))};auto compatible=vxnn::content_digest(j::str(j::member(j::member(c,"hardware"),"compatible")));
 char dir[]="/tmp/vx-hunet-presets-332-XXXXXX";require(mkdtemp(dir),"root-owned bounded fixture root");ServiceConfig config;config.private_dir=dir;unsigned opens=0;auto owner=value(DeviceOwner::acquire(config,[&](const std::string&){++opens;return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"one fake HOST owner");std::shared_ptr<NativeRouter>router;Transport*io=nullptr;
 ExecutionHooks route;route.prepare_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return router->handlers().prepare_gpu(w,b,r);};route.finish_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return router->handlers().finish_gpu(w,b,r);};route.non_gpu=[&](const WorkItem&w,UnifiedBackend&b,vxnn::CpuPool*p,ExecutionReceipt&r){return router->handlers().non_gpu(w,b,p,r);};route.retire_request=[&](RequestKey k,UnifiedBackend&b){return router->handlers().retire_request(k,b);};
 auto port=std::make_shared<ComputeExecutionPort>(owner,route,[&](int){auto t=std::make_unique<Transport>(argv[2],argv[3],compatible);io=t.get();return t;});auto hw=value(port->probe(owner->fd(),BackendMode::COMPUTE_V2),"actual C fixture capabilities");auto backend=value(port->adapter_backend(),"one backend");auto caps=value(backend->capabilities(),"frozen address domains");std::ifstream f(argv[2],std::ios::binary);std::vector<uint8_t>raw((std::istreambuf_iterator<char>(f)),{});auto catalog=value(InstalledCatalog::decode({raw.data(),raw.size()},hw,hash(raw.data(),raw.size())),"original installed catalog");auto residency=value(ProgramResidency::create(catalog,backend),"one program residency");
 auto rt=value(Runtime::start(owner,hw,vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[](const ExecutionReceipt&,DeliveryStage){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"common HOST Runtime");StartupContext root;root.borrowed_fd=owner->fd();root.identity=hw;root.compute_backend=backend;root.runtime=rt;root.results=rt->trusted_results();root.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;root.boot_id="boot";root.private_dir=dir;router=value(NativeRouter::create(root),"same common router");std::string output=std::string(dir)+"/outputs";require(!mkdir(output.c_str(),0700),"private outputs");auto files=value(NativeFiles::create(root,output),"same output owner");
 vxnn::OperatorRegistry registry;ok(vxnn::register_hunet_operators(registry,family),"original operators");auto base=value(vxnn::prepare_hunet(package,registry,caps,hw),"original actual plan");InstalledNnModel spec;spec.id="hunet-native";spec.input_id="frozen";spec.package_directory=argv[1];spec.model=package.model_id();spec.plan=base.plan_id;spec.input=package.assets().at("input.f32").sha;spec.input_bytes=196608;spec.resident_bytes=package.resident_bytes();spec.family=family;spec.job_budget_ns=60000000000ULL;
 auto library=[&](){return value(NativeNnLibrary::create(root,router,residency,files),"real native library");};auto declaration=[&](const Preset&p){auto copy=spec;copy.presets=InstalledNnPresets{p.directory,p.manifest};return copy;};

 (void)mode;Preset fixed(dir,2,1);auto lib=library();std::vector<InstalledNnModel>specifications{declaration(fixed)};
 for(int n=5;n<7;n++){
  auto p=value(vxnn::load_package(vxnn::Path(argv[n])),"actual distinct strategy package");auto copy=spec;copy.id=n==5?"hunet-gn9":"hunet-gn16";copy.package_directory=argv[n];copy.model=p.model_id();copy.input=p.assets().at("input.f32").sha;copy.resident_bytes=p.resident_bytes();
  const auto&b=*p.assets().at("binding.json").content;auto t=j::parse(std::string(b.begin(),b.end()));vxnn::HunetFamily fam{j::digest(j::member(t.get(),"code_sha256")),j::digest(j::member(t.get(),"implementation_sha256")),j::digest(j::member(t.get(),"schema_sha256"))};copy.family=fam;vxnn::OperatorRegistry operators;ok(vxnn::register_hunet_operators(operators,fam),"all strategy operators");copy.plan=value(vxnn::prepare_hunet(p,operators,caps,hw),"actual strategy Plan").plan_id;copy.presets=InstalledNnPresets{fixed.directory,fixed.manifest};specifications.push_back(copy);
 }
 std::vector<TrustedApplication>entries;
 for(const auto&v:specifications){auto made=lib->entry(v);if(!made){fprintf(stderr,"THREE_HUNET_ENTRIES_REJECTED id=%s code=%d category=%s detail=%s resident_models=%llu\n",v.id.c_str(),made.error->code,made.error->category.c_str(),made.error->detail.c_str(),(unsigned long long)rt->stats().resident_models);return 86;}entries.push_back(*made.value);}
 require(rt->stats().resident_models==0,"catalog metadata must not pin NN model slots");
 auto prepare=[&](unsigned index,const char*id){RpcRequest q;q.command="submit";q.request={"boot","three-models",id};q.entry_id=specifications[index].id;q.input_id=specifications[index].input_id;q.parameters="{}";return entries[index].prepare(q);};
 auto first=value(prepare(0,"first"),"first bound NN model");auto second=value(prepare(1,"second"),"second bound NN model");require(rt->stats().resident_models==2,"two actual bound requests retain two model leases");
 auto third=prepare(2,"third-denied");require(!third&&third.error->code==ENOSPC,"original simultaneous NN2 bound retained");
 first.source.reset();second.source.reset();require(rt->stats().resident_models==0,"unsubmitted sources release actual model leases");
 auto later=value(prepare(2,"third-later"),"third strategy after safe source release");require(rt->stats().resident_models==1,"later strategy holds exactly its model");later.source.reset();require(rt->stats().resident_models==0,"later strategy model released");
 require(!hunet_owner_bridge_starts(io->bridge),"metadata and unsubmitted requests never START");ok(rt->shutdown(),"normal runtime shutdown");ok(lib->shutdown(),"all catalogs release normally");ok(residency->shutdown(),"normal program residency shutdown");require(!hunet_owner_bridge_live(io->bridge),"no live device objects");
 puts("R11_THREE_ACTUAL_HUNET_STRATEGIES_METADATA_NO_MODEL_PIN_REQUEST_LEASES_NN2_RETAINED_HOST_PROTOCOL_ONLY_PASS");return 0;
}
