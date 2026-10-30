#include <vxunified/service.hpp>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
using namespace vxunified;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"U10_ASSERT %s:%d %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
static RequestKey key(const std::string&s="one"){return {"boot","client",s};}
static PublishedResult output(const std::string&s="one"){
 PublishedResult p;p.lease.producer=key(s);p.lease.app=AppKind::NN;p.lease.scope=vxnn::EvidenceScope::OFFLINE;p.lease.hardware[0]=1;p.lease.model[0]=2;p.lease.plan[0]=3;
 ResultArtifact a;a.path="results/final.bin";a.layout="NCHW";a.sha[0]=4;a.shape={1,1,2,2};a.bytes=16;p.lease.artifacts.push_back(a);p.lease.lease_tokens={71};p.execution_complete=true;
 p.numeric=vxnn::NumericClass::KNOWN_NUMERIC_DEVIATION;p.raw_legacy_json="{\"counter\":9007199254740993,\"result\":\"KNOWN_NUMERIC_DEVIATION\"}";return p;
}
static int owner_test(){
 char name[]="/tmp/vx-owner-XXXXXX";char*dir=mkdtemp(name);CHECK(dir);ServiceConfig cfg;cfg.private_dir=dir;
 std::atomic<int> opens{0},closes{0};DeviceOpen open_device=[&](const std::string&path){if(path!="/dev/vortex0")return Result<int>::failure({EINVAL,"TEST","wrong device"});++opens;return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));};DeviceClose close_device=[&](int fd){++closes;close(fd);};
 {
  auto a=DeviceOwner::acquire(cfg,open_device,close_device);CHECK(a);CHECK((*a.value)->fd()>=0);CHECK(fcntl((*a.value)->fd(),F_GETFD)&FD_CLOEXEC);CHECK((*a.value)->mode()==BackendMode::COMPUTE_V2);
  auto b=DeviceOwner::acquire(cfg,open_device,close_device);CHECK(!b&&b.error->code==EBUSY);CHECK(opens==1&&closes==0);
 }
 CHECK(closes==1);cfg.mode=BackendMode::LEGACY_COMPAT;{auto c=DeviceOwner::acquire(cfg,open_device,close_device);CHECK(c&&(*c.value)->mode()==BackendMode::LEGACY_COMPAT);}CHECK(opens==2&&closes==2);
 CHECK(chmod(dir,0755)==0);auto bad=DeviceOwner::acquire(cfg,open_device,close_device);CHECK(!bad&&bad.error->code==EACCES&&opens==2);CHECK(chmod(dir,0700)==0);
 CHECK(unlink((std::string(dir)+"/owner.lock").c_str())==0);CHECK(symlink("/dev/null",(std::string(dir)+"/owner.lock").c_str())==0);auto link=DeviceOwner::acquire(cfg,open_device,close_device);CHECK(!link&&opens==2);CHECK(unlink((std::string(dir)+"/owner.lock").c_str())==0);
 DeviceOpen fail=[](const std::string&){return Result<int>::failure({ENODEV,"DEVICE","first original device failure"});};auto f=DeviceOwner::acquire(cfg,fail,close_device);CHECK(!f&&f.error->code==ENODEV&&f.error->detail=="first original device failure");{auto c=DeviceOwner::acquire(cfg,open_device,close_device);CHECK(c);}CHECK(closes==3);
 CHECK(unlink((std::string(dir)+"/owner.lock").c_str())==0);CHECK(rmdir(dir)==0);puts("U10_OWNER_ONE_FD_AND_SECURE_LOCK_PASS");return 0;
}
static int results_test(){
 int persisted=0,freed=0;bool log_fails=true,retire_fails=false;std::string exact;
 ResultStore store([&](const PublishedResult&p){++persisted;exact=p.raw_legacy_json;return log_fails?Result<void>::failure({ENOSPC,"EVIDENCE","original log write failed"}):Result<void>::success();},[&](const ResultLease&l){if(l.lease_tokens!=std::vector<uint64_t>{71})return Result<void>::failure({EINVAL,"TEST","wrong backing"});if(retire_fails)return Result<void>::failure({EBUSY,"RETIRE","unknown retirement"});++freed;return Result<void>::success();});
 auto p=output();auto fail=store.publish(p);CHECK(!fail&&fail.error->code==ENOSPC);CHECK(!store.take_result(key())&&store.stats().records==0&&freed==0&&exact==p.raw_legacy_json);
 log_fails=false;CHECK(store.publish(p));auto same=store.publish(p);CHECK(same&&persisted==2);auto changed=p;changed.lease.artifacts[0].sha[1]=9;auto collision=store.publish(changed);CHECK(!collision&&collision.error->code==EEXIST&&persisted==2);
 auto a=store.take_result(key());CHECK(a&&a.value->execution_complete&&!a.value->displayed&&a.value->numeric==vxnn::NumericClass::KNOWN_NUMERIC_DEVIATION&&a.value->raw_legacy_json==p.raw_legacy_json&&!a.value->gpu_raw_cycles);
 auto dp=store.retain_result(key(),Consumer::DP),tex=store.retain_result(key(),Consumer::TEX);CHECK(dp&&tex&&dp.value->token!=tex.value->token);CHECK(store.stats().consumer_leases==2);
 DisplayReceipt shown;shown.cookie=dp.value->token;shown.presented=true;shown.result=0;CHECK(store.mark_displayed(*dp.value,shown));CHECK(store.take_result(key()).value->displayed);
 CHECK(store.reap_result(key()));CHECK(!store.take_result(key())&&freed==0&&store.stats().records==1&&store.stats().client_holds==0);
 auto stale=*dp.value;stale.producer=key("other");auto wrong=store.release_result(stale,true);CHECK(!wrong&&wrong.error->code==ESTALE);auto early=store.release_result(*dp.value,false);CHECK(!early&&early.error->code==EBUSY&&freed==0);
 CHECK(store.release_result(*dp.value,true));CHECK(freed==0);auto twice=store.release_result(*dp.value,true);CHECK(!twice&&twice.error->code==ENOENT);
 retire_fails=true;auto pin=store.release_result(*tex.value,true);CHECK(!pin&&pin.error->code==EBUSY&&store.stats().records==1&&store.stats().pinned_retire_failures==1&&freed==0);
 retire_fails=false;CHECK(store.release_result(*tex.value,true));CHECK(freed==1&&store.stats().records==0);
 CHECK(!store.publish(PublishedResult{}));p=output("two");p.lease.scope=vxnn::EvidenceScope::OFFLINE;p.gpu_raw_cycles=1;auto forged=store.publish(p);CHECK(!forged&&forged.error->code==EINVAL);p.gpu_raw_cycles.reset();p.displayed=true;CHECK(!store.publish(p));
 puts("U10_RESULT_DURABLE_PUBLISH_LEASE_AND_KNOWN_NUMERIC_PASS");return 0;
}
static int bounded_concurrent_test(){
 std::atomic<int> freed{0};ResultStore store([](const PublishedResult&){return Result<void>::success();},[&](const ResultLease&){++freed;return Result<void>::success();});
 CHECK(store.publish(output()));std::atomic<int>bad{0};std::vector<std::thread>threads;
 for(int n=0;n<4;n++){threads.emplace_back([&](){for(int i=0;i<100;i++){auto x=store.retain_result(key(),Consumer::CPU);if(!x||!store.release_result(*x.value,true))++bad;}});}
 for(auto&t:threads){t.join();}CHECK(bad==0&&freed==0&&store.stats().consumer_leases==0);
 std::vector<ConsumerLease>holds;for(int i=0;i<64;i++){auto l=store.retain_result(key(),Consumer::DP);CHECK(l);holds.push_back(*l.value);}auto full=store.retain_result(key(),Consumer::DP);CHECK(!full&&full.error->code==ENOSPC);CHECK(store.reap_result(key()));for(const auto&l:holds)CHECK(store.release_result(l,true));CHECK(freed==1);
 for(int i=0;i<256;i++){CHECK(store.publish(output(std::to_string(i))));}auto too_many=store.publish(output("overflow"));CHECK(!too_many&&too_many.error->code==ENOSPC);for(int i=0;i<256;i++){CHECK(store.reap_result(key(std::to_string(i))));}CHECK(freed==257);
 puts("U10_RESULT_BOUNDED_CONCURRENT_PASS");return 0;
}
int main(int argc,char**argv){if(argc==2&&std::string(argv[1])=="owner")return owner_test();if(argc==2&&std::string(argv[1])=="leases")return results_test();if(argc==2&&std::string(argv[1])=="concurrent")return bounded_concurrent_test();return 2;}
