#include "internal.hpp"
namespace vxunified {namespace display_detail {
bool same_source(const ResultLease&a,const ResultLease&b){
 if(a.producer.boot_id!=b.producer.boot_id||a.producer.client_id!=b.producer.client_id||a.producer.request_id!=b.producer.request_id||a.app!=b.app||a.scope!=b.scope||a.hardware!=b.hardware||a.model!=b.model||a.plan!=b.plan||a.lease_tokens!=b.lease_tokens||a.artifacts.size()!=b.artifacts.size())return false;
 for(size_t i=0;i<a.artifacts.size();i++){const auto&x=a.artifacts[i];const auto&y=b.artifacts[i];if(x.path!=y.path||x.layout!=y.layout||x.sha!=y.sha||x.shape!=y.shape||x.bytes!=y.bytes)return false;}
 return true;
}
Result<void> release(std::shared_ptr<Hold>&h){
 if(!h)return Result<void>::success();
 if(!h->lease){h.reset();return Result<void>::success();}
 auto r=h->store->release_result(*h->lease.value,true);
 if(!r){h->exit.retain(h);return r;}
 h.reset();return Result<void>::success();
}
}}
