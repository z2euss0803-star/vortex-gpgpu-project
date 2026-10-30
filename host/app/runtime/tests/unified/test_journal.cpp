#include <vxunified/journal.hpp>
#include "scheduler_fixture.hpp"
#include <json-c/json.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <cstring>
#include <fstream>
#include <type_traits>
using namespace vxunified;
namespace {
template<class T,class=void>struct StageJournal {static Result<void>save(T&,const PublishedResult&){return Result<void>::failure({ENOSYS,"TEST_MISSING_STAGE","private journal entry missing"});}};
template<class T>struct StageJournal<T,std::void_t<decltype(std::declval<T&>().save_result_stage(std::declval<const PublishedResult&>()))>> {static Result<void>save(T&j,const PublishedResult&p){return j.save_result_stage(p);}};
enum Fault {NONE,PARTIAL,FILE_SYNC,DIR_SYNC};Fault fault=NONE;int writes=0;
std::string read(const std::string&p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});}
std::vector<std::string> files(const std::string&root,bool final){std::vector<std::string>v;auto*d=opendir(root.c_str());require(d,"opendir journal fixture");while(auto*e=readdir(d)){std::string n=e->d_name;if(n=="."||n=="..")continue;bool json=n.size()>5&&n.substr(n.size()-5)==".json";if(json==final)v.push_back(root+"/"+n);}closedir(d);return v;}
ExecutionBinding binding(){ExecutionBinding b;b.hardware=digest(1);b.bit=digest(2);b.xsa=digest(3);b.compatible=digest(4);b.model=digest(5);b.plan=digest(6);b.input=digest(7);b.source=digest(8);b.device_generation=1;b.provider_flags=1;b.input_bytes=44;return b;}
ExecutionReceipt receipt(){ExecutionReceipt r;r.binding=binding();r.outcome.request={"boot","client","request"};r.outcome.node=7;r.outcome.work_index=3;r.outcome.status=RequestStatus::RUNNING;r.submitted_cookie=9007199254740993ULL;r.submitted_sequence=91;r.device_generation=1;r.program_handle=2;r.program_generation=3;r.submit_begin_ns=11;r.submit_end_ns=12;r.prepare_begin_ns=7;r.prepare_end_ns=9;r.execute_begin_ns=10;r.execute_end_ns=15;r.execution_error=vxnn::Error{-ETIMEDOUT,"PORT","first timeout exact"};vxl_v2_completion d{};d.version=2;d.bytes=256;d.cookie=r.submitted_cookie;d.sequence=91;d.reserved[0]=0xab;r.linux_detail=d;return r;}
std::vector<unsigned char> unhex(const char*p){std::vector<unsigned char>b;require(std::strlen(p)%2==0,"even hex");for(size_t i=0;p[i];i+=2){unsigned v=0;require(std::sscanf(p+i,"%2x",&v)==1,"decode hex");b.push_back(static_cast<unsigned char>(v));}return b;}
json_object*member(json_object*p,const char*k){json_object*r=nullptr;require(json_object_object_get_ex(p,k,&r),k);return r;}
}
extern "C" ssize_t __real_write(int,const void*,size_t);
extern "C" int __real_fsync(int);
extern "C" int __real_fdatasync(int);
extern "C" ssize_t __wrap_write(int fd,const void*p,size_t n){if(fault==PARTIAL){if(writes++==0)return __real_write(fd,p,std::min(n,size_t(7)));errno=ENOSPC;return -1;}return __real_write(fd,p,n);}
extern "C" int __wrap_fdatasync(int fd){if(fault==FILE_SYNC){errno=EIO;return -1;}return __real_fdatasync(fd);}
extern "C" int __wrap_fsync(int fd){struct stat s{};if(fault==DIR_SYNC&&fstat(fd,&s)==0&&S_ISDIR(s.st_mode)){errno=EIO;return -1;}return __real_fsync(fd);}
int main(int argc,char**argv){
 require(argc==3,"root/mode required");std::string base=argv[1],mode=argv[2];if(mkdir(base.c_str(),0700)<0)require(errno==EEXIST,"create journal root");char absolute[PATH_MAX];require(realpath(base.c_str(),absolute),"absolute test root");auto dir=std::string(absolute)+"/"+mode;require(mkdir(dir.c_str(),0700)==0,"fresh per-case journal directory");
 if(mode=="unsafe"){require(chmod(dir.c_str(),0755)==0,"unsafe fixture mode");require(!EvidenceJournal::open(dir),"unsafe journal directory rejected");std::puts("U10_JOURNAL_UNSAFE_DIRECTORY_REJECTED_NOT_BOARD");return 0;}
 auto journal=value(EvidenceJournal::open(dir),"secure journal opens without a device");auto r=receipt();r.program_entry_id=digest(10);

 if(mode=="joint"){
  auto hex=[](const char*s){Digest d{};for(unsigned i=0;i<32;i++){auto n=[](char c){return unsigned(c<='9'?c-'0':c-'a'+10);};d[i]=(n(s[i*2])<<4)|n(s[i*2+1]);}return d;};
  auto&b=r.binding;b.provider_flags=4;b.hardware=hex("1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d");b.bit=hex("94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1");b.xsa=hex("02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94");b.compatible=hex("f670c653a37361f05f57c14eecd53dea1b87556489a2c1314efd208d715826dc");
  ok(journal->save_work(r,DeliveryStage::EXECUTION_ERROR),"real journal must persist exact joint provider work binding");
  auto saved=files(dir,true);require(saved.size()==1,"joint receipt durable");auto*o=json_tokener_parse(read(saved[0]).c_str());require(o&&json_object_get_int(member(member(o,"binding"),"provider_flags"))==4,"actual joint flag in journal JSON");json_object_put(o);
  auto bad=r;bad.binding.bit[0]^=1;require(!journal->save_work(bad,DeliveryStage::EXECUTION_ERROR),"joint digest mismatch rejected before persistence");
 }else if(mode=="work"){

  auto second=EvidenceJournal::open(dir);require(!second&&second.error->code==EBUSY,"journal has one filesystem writer");ok(journal->save_work(r,DeliveryStage::EXECUTION_ERROR),"persist failed work");ok(journal->save_work(r,DeliveryStage::EXECUTION_ERROR),"idempotent durable retry");auto names=files(dir,true);require(names.size()==1,"one immutable final event");struct stat st{};require(lstat(names[0].c_str(),&st)==0&&(st.st_mode&0777)==0600,"private event mode");auto raw=read(names[0]);auto*p=json_tokener_parse(raw.c_str());require(p,"event JSON readable");require(json_object_get_uint64(member(p,"submitted_cookie"))==9007199254740993ULL,"u64 never goes through double");require(json_object_get_int(member(member(p,"binding"),"provider_flags"))==1,"explicit provider identity retained");require(member(p,"gpu_raw_cycles")==nullptr,"offline cycles null");require(std::string(json_object_get_string(member(p,"program_catalog_entry_id")))==std::string("0a")+std::string(62,'0'),"program catalog identity encoded before WAIT completion");auto b=unhex(json_object_get_string(member(p,"linux_completion_hex")));require(b.size()==256&&std::memcmp(b.data(),&*r.linux_detail,256)==0,"all raw driver bytes retained");require(std::string(json_object_get_string(member(member(p,"execution_error"),"detail")))=="first timeout exact","original error exact");require(json_object_get_uint64(member(member(p,"timing_ns"),"prepare_begin"))==7&&json_object_get_uint64(member(member(p,"timing_ns"),"prepare_end"))==9,"dispatch preparation interval persisted exactly");json_object_put(p);
 }else if(mode=="stage"){
  PublishedResult p;p.binding=binding();p.execution_complete=true;p.numeric=vxnn::NumericClass::KNOWN_NUMERIC_DEVIATION;p.lease.producer=r.outcome.request;p.lease.hardware=p.binding->hardware;p.lease.model=p.binding->model;p.lease.plan=p.binding->plan;p.raw_legacy_json="{\"counter\":9007199254740993}";ResultArtifact a;a.path="final.bin";a.layout="NCHW";a.shape={11};a.bytes=44;a.sha=digest(9);p.lease.artifacts={a};
  auto first=StageJournal<EvidenceJournal>::save(*journal,p);require(bool(first),"U20 private display staging needs a distinct durable journal record");p.displayed=true;ok(StageJournal<EvidenceJournal>::save(*journal,p),"private display proof journal");ok(journal->save_result(p),"separate final commit journal");
  auto names=files(dir,true);require(names.size()==3,"private original/displayed and committed records are separate immutable files");int staged=0,committed=0;
  for(const auto&name:names){auto*q=json_tokener_parse(read(name).c_str());require(q,"staged actual filesystem JSON");std::string kind=json_object_get_string(member(q,"kind"));if(kind=="result_stage"){++staged;require(!json_object_get_boolean(member(q,"client_visible")),"stage never reports client visibility");}else{require(kind=="result","unchanged public result record kind");++committed;}auto raw=unhex(json_object_get_string(member(q,"legacy_json_hex")));require(std::string(raw.begin(),raw.end())==p.raw_legacy_json&&member(q,"gpu_raw_cycles")==nullptr&&json_object_get_int(member(q,"numeric"))==1,"original numeric/raw integer and offline null kept");json_object_put(q);}
  require(staged==2&&committed==1,"stage and commit evidence cannot be confused");
 }else if(mode=="result"){
  PublishedResult p;p.binding=binding();p.execution_complete=true;p.numeric=vxnn::NumericClass::KNOWN_NUMERIC_DEVIATION;p.lease.producer=r.outcome.request;p.lease.hardware=p.binding->hardware;p.lease.model=p.binding->model;p.lease.plan=p.binding->plan;p.raw_legacy_json="{\"counter\":9007199254740993}";ResultArtifact a;a.path="final.bin";a.layout="NCHW";a.shape={11};a.bytes=44;a.sha=digest(9);p.lease.artifacts={a};ok(journal->save_result(p),"persist result without display/accuracy promotion");auto names=files(dir,true);require(names.size()==1,"one result record");auto*q=json_tokener_parse(read(names[0]).c_str());require(q,"result JSON");auto b=unhex(json_object_get_string(member(q,"legacy_json_hex")));require(std::string(b.begin(),b.end())==p.raw_legacy_json,"legacy original bytes retained");require(!json_object_get_boolean(member(q,"displayed"))&&json_object_get_int(member(q,"numeric"))==1,"numeric and display separate");json_object_put(q);
 }else if(mode=="partial"||mode=="file-sync"||mode=="dir-sync"){
  fault=mode=="partial"?PARTIAL:mode=="file-sync"?FILE_SYNC:DIR_SYNC;auto failed=journal->save_work(r,DeliveryStage::EXECUTION_ERROR);fault=NONE;require(!failed&&(failed.error->code==ENOSPC||failed.error->code==EIO),"real write/sync failure reported");auto partials=files(dir,false);require(!partials.empty(),"failed original partial evidence retained");auto original=read(partials[0]);require(!original.empty(),"original failed bytes exist");ok(journal->save_work(r,DeliveryStage::EXECUTION_ERROR),"retry journal only");require(files(dir,true).size()==1&&read(partials[0])==original,"failed partial not overwritten on successful retry");
 }else if(mode=="mutation"){
  ok(journal->save_work(r,DeliveryStage::EXECUTION_ERROR),"initial record");auto name=files(dir,true).at(0);int fd=open(name.c_str(),O_WRONLY|O_TRUNC|O_NOFOLLOW);require(fd>=0&&write(fd,"changed",7)==7,"mutate only synthetic journal event");close(fd);auto changed=journal->save_work(r,DeliveryStage::EXECUTION_ERROR);require(!changed&&read(name)=="changed","existing hash mismatch never overwritten");
 }else{return 2;}
 std::printf("U10_JOURNAL_%s_REAL_FILESYSTEM_PASS_NOT_BOARD\n",mode.c_str());return 0;
}
