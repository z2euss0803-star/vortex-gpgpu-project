// Force the optional NRVO path off and inject each allocation failure. A lease
// cannot become unreachable just because copying its return value failed.
#include <vxunified/service.hpp>
#include <cstdio>
#include <cstdlib>
#include <new>
using namespace vxunified;
static int fail_after=-1;
void*operator new(std::size_t n){if(fail_after==0){fail_after=-1;throw std::bad_alloc();}if(fail_after>0)--fail_after;auto*p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"U10_ALLOC_ASSERT %d %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
 for(int fail=0;fail<48;fail++){
  ResultStore store([](const PublishedResult&){return Result<void>::success();},[](const ResultLease&){return Result<void>::success();});PublishedResult p;
  p.lease.producer={std::string(100,'b'),std::string(100,'c'),std::string(100,'r')};p.execution_complete=true;p.lease.hardware[0]=1;p.lease.model[0]=2;p.lease.plan[0]=3;ResultArtifact a;a.path="final.bin";a.layout="NCHW";a.shape={1};a.bytes=4;a.sha[0]=4;p.lease.artifacts={a};CHECK(store.publish(p));
  bool escaped=false,success=false;ConsumerLease held;auto request=p.lease.producer;
  fail_after=fail;
  try{auto out=store.retain_result(std::move(request),Consumer::CPU);fail_after=-1;success=bool(out);if(out)held=*out.value;else CHECK(out.error->code==ENOMEM);}catch(...){fail_after=-1;escaped=true;}
  CHECK(!escaped);CHECK(store.stats().consumer_leases==(success?1u:0u));
  if(success){CHECK(store.release_result(held,true));}CHECK(store.reap_result(p.lease.producer));CHECK(store.stats().records==0);
 }
 puts("U10_RESULT_ALLOCATION_TRANSACTION_PASS_NOT_BOARD");
}
