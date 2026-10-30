// HOST instruction-model integration through actual C object/program/ACK owner.
// Never physical GPU execution, DP presentation or device performance evidence.
#include <vxunified/image_request.hpp>
#include "scheduler_fixture.hpp"
#include "image_owner_bridge.h"
#include <vxl/hunet.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <cstring>
#include <time.h>
namespace {
std::vector<uint8_t>read(const char*p){std::ifstream f(p,std::ios::binary);require(bool(f),"frozen file");return {(std::istreambuf_iterator<char>(f)),{}};}
uint64_t ns(){timespec t{};require(!clock_gettime(CLOCK_MONOTONIC,&t),"clock");return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;}
struct Transport:IoctlTransport {void*bridge;Transport(const char*c,const char*d,Digest compatible):bridge(image_owner_bridge_create(c,d,compatible.data())){}~Transport(){image_owner_bridge_destroy(bridge);}int call(unsigned long c,void*p)override{return image_owner_bridge_call(bridge,c,p);}uint64_t now_ns()override{return ns();}};
}
int main(int argc,char**argv){
 require(argc==6,"mode/input/requests/output/table");std::string mode=argv[1],out=argv[4];bool full=mode=="full";uint32_t width=full?1280:256,height=full?720:144;auto base=read(argv[2]);require(base.size()==size_t(width)*height*4,"exact RGBA input");
 char dir[]="/tmp/vx-image-pixels-XXXXXX";require(mkdtemp(dir)!=nullptr,"private owner");ServiceConfig config;config.private_dir=dir;
 auto owner=value(DeviceOwner::acquire(config,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"sole fd");
 std::shared_ptr<ImageWorkRequest>app;Transport*io=nullptr;ExecutionHooks hooks;
 hooks.prepare_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return app->handlers().prepare_gpu(w,b,r);};
 hooks.finish_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return app->handlers().finish_gpu(w,b,r);};
 hooks.non_gpu=[&](const WorkItem&w,UnifiedBackend&b,vxnn::CpuPool*p,ExecutionReceipt&r){return app->handlers().non_gpu(w,b,p,r);};
 hooks.retire_request=[&](RequestKey k,UnifiedBackend&b){return app->handlers().retire_request(k,b);};
 auto port=std::make_shared<ComputeExecutionPort>(owner,hooks,[&](int fd){require(fd==owner->fd(),"borrowed shared fd");auto p=std::make_unique<Transport>("data/image/programs-v2/catalog.bin","data/image/programs-v2",vxnn::content_digest("vortex,hw4-s8-demo-v1"));io=p.get();return p;});
 auto hw=value(port->probe(owner->fd(),BackendMode::COMPUTE_V2),"actual C caps");auto backend=value(port->adapter_backend(),"shared backend");auto caps=value(backend->capabilities(),"capabilities");
 auto raw=read("data/image/programs-v2/catalog.bin");Digest sum{};vxl_hunet_hash(raw.data(),raw.size(),sum.data());auto catalog=value(InstalledCatalog::decode({raw.data(),raw.size()},hw,sum),"exact catalog");auto residency=value(ProgramResidency::create(catalog,backend),"program residency");auto table=read(argv[5]);require(table.size()==65536,"full immutable table");
 image_owner_bridge_arithmetic(io->bridge);
 auto session=value(ImageSession::create(catalog,caps,residency,digest(7),digest(8),{table.data(),table.size()},vxnn::EvidenceScope::HOST_PROTOCOL_TEST,true),"image owner");
 auto runtime=value(Runtime::start(owner,hw,vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[](const ExecutionReceipt&r,DeliveryStage stage){require(stage!=DeliveryStage::EXECUTION_ERROR&&!r.outcome.result,"no native owner error");return Result<void>::success();},[](const PublishedResult&p){require(!p.displayed&&!p.gpu_raw_cycles&&p.numeric==vxnn::NumericClass::NOT_EVALUATED,"host output scope");return Result<void>::success();}),"shared runtime");
 ImageRequest request;request.width=width;request.height=height;request.rectangle=full?image_rect{0,0,width,height}:image_rect{3,4,247,131};
 unsigned active=0,total_jobs=0,hits=0,count=0;std::string name;std::ofstream traces(out+"/native-traces.bin",std::ios::binary),times(out+"/owner-times.tsv");require(bool(traces)&&bool(times),"durable raw sinks");times<<"request\tjobs\tallocate_ns\tprogram_ns\tupload_ns\targs_ns\tguard_ns\tdownload_ns\tpublish_ns\trelease_ns\thost_elapsed_ns\tgpu_raw_cycles\n";
 auto set_input=[&](unsigned id,bool alter){auto data=std::make_shared<std::vector<uint8_t>>(base);if(alter)for(size_t i=0;i<data->size();i+=4){unsigned x=(i/4)%width,y=i/(4*width);if(x>=3&&x<250&&y>=4&&y<135)(*data)[i]^=uint8_t(id*17);else{(*data)[i]=7;(*data)[i+1]=16;(*data)[i+2]=30;}}request.rgba=data;request.input_id=id;request.crc=image_crc32({data->data(),data->size()});active=id;};
 auto render=[&](image_params params,const std::string&file){
  request.params=params;request.request={"boot","oracle",std::to_string(++count)};request.cookie_namespace=count+1;request.display_cookie=count+100;name=file;
  unsigned seen=0,published=0;uint64_t program=0,allocate=0,upload=0,args=0,guard=0;auto begin=ns();
  ImageTraceSink trace=[&](const RequestKey&key,const ImageJobTrace&t){require(key.request_id==request.request.request_id&&t.index==seen++&&!t.error&&t.linux_detail&&!t.gpu_raw_cycles&&!t.linux_detail->cycles_valid,"exact original native completion scope");auto bytes=value(encode_image_job_trace(t),"trace512");traces.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());traces.flush();require(bool(traces),"persist original trace before retirement");program+=t.program_ns;allocate+=t.allocate_ns;upload+=t.upload_ns;args+=t.args_ns;guard+=t.guard_ns;return Result<void>::success();};
  ImageOutputSink output=[&](const WorkRequest&w,vxnn::Bytes b,const ImageOwnerReceipt&r){published++;require(b.size==base.size()&&seen==r.completed_jobs,"all traces precede output");std::ofstream f(out+"/"+name,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data),b.size);f.flush();require(bool(f),"durable RGBA");ImagePresentation p;p.display.cookie=request.display_cookie;p.display.clock_domain=vxnn::ClockDomain::HOST_MONOTONIC;p.display.submitted_ns=10;p.display.presented_ns=20;p.display.presented=true;p.output.execution_complete=true;p.output.lease.producer=w.request;p.output.lease.app=AppKind::IMAGE;p.output.lease.hardware=hw.hardware_contract;p.output.lease.model=w.model;p.output.lease.plan=w.plan;p.output.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;ResultArtifact a;a.path=name;a.layout="RGBA8888";a.shape={height,width,4};a.bytes=b.size;vxl_hunet_hash(b.data,b.size,a.sha.data());p.output.lease.artifacts={a};return Result<ImagePresentation>::success(p);};
  app=value(session->prepare(request,60000000000ULL,trace,output),"prepare native request");auto n=app->plan().total_jobs;value(runtime->submit(app->request()),"native admission");for(uint64_t i=0;i<app->size();i++)ok(runtime->run_one(),"native job/transfer");
  require(seen==n&&published==1&&app->receipt().retired&&app->receipt().published&&!app->receipt().gpu_raw_cycles&&!residency->stats().owned,"complete bounded owner");total_jobs+=n;hits+=!n&&params.style!=0;
  const auto&r=app->receipt();times<<count<<'\t'<<n<<'\t'<<allocate<<'\t'<<program<<'\t'<<upload<<'\t'<<args<<'\t'<<guard<<'\t'<<r.download_ns<<'\t'<<r.publish_ns<<'\t'<<r.release_ns<<'\t'<<ns()-begin<<"\tnull\n";times.flush();require(bool(times),"Linux/host time evidence");ok(runtime->reap_result(request.request),"consumer retirement");return n;
 };
 if(mode=="replay") {std::ifstream f(argv[3]);unsigned tx,id,style,preset,block;while(f>>tx>>id>>style>>preset>>block){if(id!=active)set_input(id,true);render({style,preset,block},"tx-"+std::to_string(tx)+".rgba");}require(count==51&&total_jobs==87&&hits==10,"frozen replay87jobs10hits");}
 else if(full){set_input(1,false);for(unsigned s=1;s<=4;s++){image_params p{s,(s==1||s==4)?1u:0u,s==4?4u:0u};render(p,"style-"+std::to_string(s)+".rgba");require(!render(p,"cache-"+std::to_string(s)+".rgba"),"full cache no GPU");}}
 else if(mode=="first"){set_input(1,false);render({3,0,0},"style-3-p0.rgba");}
 else {require(mode=="colors","known mode");set_input(1,false);const unsigned styles[]={3,2,1,4};for(unsigned s:styles)for(unsigned p=0;p<(s==2?1u:7u);p++){image_params v{s,p,s==4?8u:0u};render(v,"style-"+std::to_string(s)+"-p"+std::to_string(p)+".rgba");require(!render(v,"cache-"+std::to_string(s)+"-p"+std::to_string(p)+".rgba"),"cache0job");}for(unsigned b=2;b<=4;b+=2)for(unsigned p=0;p<7;p++)render({4,p,b},"style-4-p"+std::to_string(p)+"-b"+std::to_string(b)+".rgba");}
 render({0,0,0},"original.rgba");require(image_owner_bridge_starts(io->bridge)==total_jobs&&image_owner_bridge_acks(io->bridge)==total_jobs,"every actual C start ACKed");ok(runtime->shutdown(),"runtime closes");ok(residency->shutdown(),"programs retire");require(!image_owner_bridge_live(io->bridge),"no objects leaked");std::remove((std::string(dir)+"/owner.lock").c_str());rmdir(dir);
 printf("U14_IMAGE_NATIVE_OWNER_WORDS_PASS_HOST mode=%s jobs=%u hits=%u gpu_raw_cycles=null\n",mode.c_str(),total_jobs,hits);
}
