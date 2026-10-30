#include <vxunified/service.hpp>
#include <vxunified/exit_retention.hpp>
#include <map>
#include <mutex>
#include <set>
namespace vxunified {
namespace {
std::string key(const RequestKey&k){return k.boot_id+'\0'+k.client_id+'\0'+k.request_id;}
bool identifier(const std::string&s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c==':';});}
bool valid(const PublishedResult&p){
 auto&l=p.lease;auto&k=l.producer;if(!identifier(k.boot_id)||!identifier(k.client_id)||!identifier(k.request_id)||!p.execution_complete||p.displayed||!nonzero_digest(l.hardware)||!nonzero_digest(l.model)||!nonzero_digest(l.plan)||l.artifacts.empty()||l.artifacts.size()>64||l.lease_tokens.size()>64||p.raw_legacy_json.size()>65536)return false;
 if(static_cast<unsigned>(l.app)<1||static_cast<unsigned>(l.app)>3||static_cast<unsigned>(l.scope)>static_cast<unsigned>(vxnn::EvidenceScope::REAL_BOARD)||static_cast<unsigned>(p.numeric)>static_cast<unsigned>(vxnn::NumericClass::NOT_EVALUATED))return false;
 if(p.gpu_raw_cycles&&l.scope!=vxnn::EvidenceScope::REAL_BOARD)return false;
 if(p.binding){auto&b=*p.binding;bool provider=(b.provider_flags==4&&(b.app==AppKind::NN||b.app==AppKind::P8||b.app==AppKind::IMAGE))||(b.provider_flags==1&&(b.app==AppKind::NN||b.app==AppKind::IMAGE))||(b.provider_flags==2&&b.app==AppKind::P8);if(!provider||b.app!=l.app||b.hardware!=l.hardware||b.model!=l.model||b.plan!=l.plan||!b.device_generation||!nonzero_digest(b.bit)||!nonzero_digest(b.xsa)||!nonzero_digest(b.compatible)||!nonzero_digest(b.input)||!nonzero_digest(b.source))return false;}
 std::set<std::string>paths;std::set<uint64_t>tokens;for(auto t:l.lease_tokens){if(!t||!tokens.insert(t).second)return false;}
 for(const auto&a:l.artifacts){
  if(a.path.empty()||a.path.size()>4096||a.path[0]=='/'||a.path.find('\0')!=std::string::npos||a.path.find('\\')!=std::string::npos||a.layout.empty()||a.layout.size()>64||a.layout.find('\0')!=std::string::npos||!nonzero_digest(a.sha)||!a.bytes||a.bytes>(1ULL<<30)||a.shape.empty()||a.shape.size()>8||!paths.insert(a.path).second)return false;
  size_t begin=0;while(begin<=a.path.size()){auto end=a.path.find('/',begin);auto part=a.path.substr(begin,end==std::string::npos?end:end-begin);if(part.empty()||part=="."||part=="..")return false;if(end==std::string::npos)break;begin=end+1;}
  uint64_t elements=1;for(auto d:a.shape){if(!d||d>UINT64_MAX/elements)return false;elements*=d;}
 }
 return true;
}
bool same(const PublishedResult&a,const PublishedResult&b){
 if(a.binding.has_value()!=b.binding.has_value()||(a.binding&&!(*a.binding==*b.binding)))return false;
 auto&x=a.lease;auto&y=b.lease;if(key(x.producer)!=key(y.producer)||x.app!=y.app||x.scope!=y.scope||x.hardware!=y.hardware||x.model!=y.model||x.plan!=y.plan||x.lease_tokens!=y.lease_tokens||x.artifacts.size()!=y.artifacts.size()||a.numeric!=b.numeric||a.raw_legacy_json!=b.raw_legacy_json||a.execution_complete!=b.execution_complete||a.gpu_raw_cycles!=b.gpu_raw_cycles)return false;
 for(size_t i=0;i<x.artifacts.size();i++){auto&p=x.artifacts[i];auto&q=y.artifacts[i];if(p.path!=q.path||p.layout!=q.layout||p.sha!=q.sha||p.shape!=q.shape||p.bytes!=q.bytes)return false;}return true;
}
template<class F>Result<void> callback(F fn){try{return fn();}catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"SERVICE","result callback allocation"});}catch(...){return Result<void>::failure({EIO,"SERVICE","result callback exception; backing retained"});}}
}
Result<void> validate_published_result(const PublishedResult&p){
 try{if(!valid(p))return Result<void>::failure({EINVAL,"SERVICE","invalid published result metadata"});return Result<void>::success();}
 catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"SERVICE","result metadata validation allocation"});}
}
struct ResultStore::State {
 detail::ExitRetention exit_retention;
 struct Hold {Consumer consumer=Consumer::CPU;uint64_t cookie=0;bool bound=false;};
 struct Record {PublishedResult result;bool client=true,retire_failed=false,staged=false;PersistResult stage_journal;std::map<uint64_t,Hold>holds;};
 std::mutex mutex;std::map<std::string,Record> records;uint64_t serial=0;size_t consumers=0;PersistResult persist;RetireResult retire;
 Result<void> collect(std::map<std::string,Record>::iterator it){auto&e=it->second;if(e.staged||e.client||!e.holds.empty())return Result<void>::success();auto r=callback([&](){return retire(e.result.lease);});if(!r){e.retire_failed=true;return r;}records.erase(it);return Result<void>::success();}
};
ResultStore::ResultStore(PersistResult p,RetireResult r):state_(std::make_shared<State>()){state_->persist=std::move(p);state_->retire=std::move(r);}
ResultStore::~ResultStore(){
 // Unknown consumers/retirement must never trigger a guessed backend release.
 // Retain callbacks (including borrowed owner's shared lifetime) until exit.
 try{std::lock_guard<std::mutex>lock(state_->mutex);if(!state_->records.empty())state_->exit_retention.retain(state_);}
 catch(...){state_->exit_retention.retain(state_);}
}
Result<void> ResultStore::publish(const PublishedResult&p){
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);try{
  if(!s.persist||!s.retire||!valid(p))return Result<void>::failure({EINVAL,"SERVICE","invalid result receipt or absent persistence/retirement"});
  auto k=key(p.lease.producer);auto it=s.records.find(k);if(it!=s.records.end()){
   if(!same(it->second.result,p))return Result<void>::failure({EEXIST,"SERVICE","producer reused with different output"});
   if(it->second.staged){auto saved=callback([&](){return s.persist(it->second.result);});if(!saved)return saved;it->second.staged=false;it->second.client=true;it->second.stage_journal={};}
   return Result<void>::success();
  }
  if(s.records.size()>=256)return Result<void>::failure({ENOSPC,"SERVICE","retained result index256"});
  // Allocate before durable evidence; a failed allocation cannot report publish.
  State::Record record;record.result=p;auto placed=s.records.emplace(k,std::move(record));auto persisted=callback([&](){return s.persist(p);});if(!persisted){s.records.erase(placed.first);return persisted;}return Result<void>::success();
 }catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"SERVICE","result publication allocation"});}
}
Result<void>ResultStore::stage_display(const PublishedResult&p,PersistResult journal){
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);try{
  if(!journal||!s.persist||!s.retire||!p.binding||!valid(p))return Result<void>::failure({EINVAL,"SERVICE_STAGE","bound checked output and distinct stage journal required"});
  auto k=key(p.lease.producer);auto it=s.records.find(k);if(it!=s.records.end()){
   if(!same(it->second.result,p))return Result<void>::failure({EEXIST,"SERVICE_STAGE","producer reused with different stage"});
   return it->second.staged?Result<void>::success():Result<void>::failure({EALREADY,"SERVICE_STAGE","producer already publicly committed"});
  }
  if(s.records.size()>=256)return Result<void>::failure({ENOSPC,"SERVICE_STAGE","common retained result index256"});
  State::Record r;r.result=p;r.client=false;r.staged=true;r.stage_journal=std::move(journal);auto placed=s.records.emplace(k,std::move(r));
  auto saved=callback([&](){return placed.first->second.stage_journal(p);});if(!saved){s.records.erase(placed.first);return saved;}return Result<void>::success();
 }catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"SERVICE_STAGE","private output staging allocation"});}
}
Result<PublishedResult> ResultStore::take_result(RequestKey k)const{
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);try{auto it=s.records.find(key(k));if(it==s.records.end()||!it->second.client)return Result<PublishedResult>::failure({ENOENT,"SERVICE","no client result hold"});return Result<PublishedResult>::success(it->second.result);}catch(const std::bad_alloc&){return Result<PublishedResult>::failure({ENOMEM,"SERVICE","result delivery allocation"});}
}
Result<ConsumerLease> ResultStore::retain_result(RequestKey k,Consumer c){
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);try{
  auto it=s.records.find(key(k));if(it==s.records.end()||(!it->second.client&&!(it->second.staged&&c==Consumer::DP)))return Result<ConsumerLease>::failure({ENOENT,"SERVICE","no client result hold or private DP stage"});if(static_cast<unsigned>(c)>static_cast<unsigned>(Consumer::DP))return Result<ConsumerLease>::failure({EINVAL,"SERVICE","invalid consumer"});
  if(s.consumers>=64){return Result<ConsumerLease>::failure({ENOSPC,"SERVICE","consumer leases64"});}
  uint64_t candidate=s.serial;unsigned skipped=0;
  for(;;){
   if(candidate==UINT64_MAX)return Result<ConsumerLease>::failure({EOVERFLOW,"SERVICE","consumer serial exhausted"});
   ++candidate;bool collision=false;
   if(c==Consumer::DP)for(const auto&record:s.records)for(const auto&hold:record.second.holds)collision|=hold.second.consumer==Consumer::DP&&hold.second.cookie==candidate;
   if(!collision)break;
   if(++skipped>64)return Result<ConsumerLease>::failure({EOVERFLOW,"SERVICE","bounded DP cookie search exhausted"});
  }
  ConsumerLease out;out.producer=k;out.consumer=c;out.token=candidate;it->second.holds.emplace(out.token,State::Hold{c,out.token,false});s.serial=candidate;++s.consumers;
  // C++17 guaranteed prvalue elision plus string moves: after committing the
  // lease, delivery must not allocate/copy a const Result and lose its token.
  return Result<ConsumerLease>::success(std::move(out));
 }catch(const std::bad_alloc&){return Result<ConsumerLease>::failure({ENOMEM,"SERVICE","consumer lease allocation"});}
}
Result<PublishedResult>ResultStore::display_source(const ConsumerLease&l)const{
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);try{
  auto it=s.records.find(key(l.producer));if(it==s.records.end())return Result<PublishedResult>::failure({ESTALE,"SERVICE_STAGE","display source no longer retained"});auto h=it->second.holds.find(l.token);
  if(h==it->second.holds.end()||l.consumer!=Consumer::DP||h->second.consumer!=Consumer::DP)return Result<PublishedResult>::failure({ESTALE,"SERVICE_STAGE","exact retained DP lease required"});
  return Result<PublishedResult>::success(it->second.result);
 }catch(const std::bad_alloc&){return Result<PublishedResult>::failure({ENOMEM,"SERVICE_STAGE","display source delivery allocation"});}
}
Result<void>ResultStore::bind_display_cookie(const ConsumerLease&l,uint64_t cookie){
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);try{
  auto it=s.records.find(key(l.producer));if(it==s.records.end())return Result<void>::failure({ESTALE,"SERVICE_STAGE","display producer missing"});auto h=it->second.holds.find(l.token);
  if(!cookie||h==it->second.holds.end()||l.consumer!=Consumer::DP||h->second.consumer!=Consumer::DP)return Result<void>::failure({EINVAL,"SERVICE_STAGE","nonzero cookie and exact DP lease required"});
  if(h->second.bound)return h->second.cookie==cookie?Result<void>::success():Result<void>::failure({ESTALE,"SERVICE_STAGE","logical presentation cookie already bound"});
  for(const auto&record:s.records)for(const auto&hold:record.second.holds)if(hold.first!=l.token&&hold.second.consumer==Consumer::DP&&hold.second.cookie==cookie)return Result<void>::failure({EEXIST,"SERVICE_STAGE","logical presentation cookie already held"});
  h->second.cookie=cookie;h->second.bound=true;return Result<void>::success();
 }catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"SERVICE_STAGE","display cookie binding allocation"});}
}
Result<void> ResultStore::release_result(const ConsumerLease&l,bool retired){
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);try{
  auto it=s.records.find(key(l.producer));if(it==s.records.end())return Result<void>::failure({ESTALE,"SERVICE","producer no longer retained"});auto h=it->second.holds.find(l.token);if(h==it->second.holds.end())return Result<void>::failure({ENOENT,"SERVICE","unknown consumer token"});if(h->second.consumer!=l.consumer)return Result<void>::failure({ESTALE,"SERVICE","consumer differs"});if(!retired)return Result<void>::failure({EBUSY,"SERVICE","consumer retirement not confirmed"});
  auto&e=it->second;if(!e.staged&&!e.client&&e.holds.size()==1){auto r=callback([&](){return s.retire(e.result.lease);});if(!r){e.retire_failed=true;return r;}--s.consumers;s.records.erase(it);return Result<void>::success();}
  e.holds.erase(h);--s.consumers;return Result<void>::success();
 }catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"SERVICE","consumer release allocation; hold retained"});}
}
Result<void> ResultStore::mark_displayed(const ConsumerLease&l,const DisplayReceipt&r){
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);try{
  auto it=s.records.find(key(l.producer));if(it==s.records.end())return Result<void>::failure({ESTALE,"SERVICE","display producer missing"});auto h=it->second.holds.find(l.token);
  if(h==it->second.holds.end()||l.consumer!=Consumer::DP||h->second.consumer!=Consumer::DP||r.cookie!=h->second.cookie)return Result<void>::failure({ESTALE,"SERVICE","display lease/cookie mismatch"});
  const bool clock_ok=r.clock_domain==vxnn::ClockDomain::LINUX_MONOTONIC||(r.clock_domain==vxnn::ClockDomain::HOST_MONOTONIC&&it->second.result.lease.scope==vxnn::EvidenceScope::HOST_PROTOCOL_TEST);
  if(!r.presented||r.result||!clock_ok)return Result<void>::failure({EINVAL,"SERVICE","no successful presentation receipt in permitted clock scope"});
  if(it->second.result.displayed){return Result<void>::success();}
  auto next=it->second.result;next.displayed=true;auto p=callback([&](){return it->second.staged?it->second.stage_journal(next):s.persist(next);});if(!p)return p;it->second.result.displayed=true;return Result<void>::success();
 }catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"SERVICE","presentation evidence allocation"});}
}
Result<void> ResultStore::reap_result(RequestKey k){auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);try{auto it=s.records.find(key(k));if(it==s.records.end())return Result<void>::failure({ENOENT,"SERVICE","result missing"});if(it->second.staged)return Result<void>::failure({EBUSY,"SERVICE_STAGE","private stage is not a client completion"});it->second.client=false;return s.collect(it);}catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"SERVICE","result reap allocation; backing retained"});}}
ResultStoreStats ResultStore::stats()const{auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);ResultStoreStats out;out.records=s.records.size();out.consumer_leases=s.consumers;for(const auto&p:s.records){if(p.second.client)++out.client_holds;if(p.second.retire_failed)++out.pinned_retire_failures;if(p.second.staged)++out.staged_records;}return out;}
}
