#include <vxunified/resources.hpp>
#include <cassert>
#include <thread>
#include <atomic>
#include <iostream>
using namespace vxunified;
int main(){
 vxl_v2_ctx c;vxl_v2_init(&c,nullptr,nullptr);assert(vxl_v2_owner_enter(&c,1,1)==0);
 vxl_v2_ref r{};assert(vxl_v2_buffer_create(&c,1,1,2,3,3,64,&r)==0);
 std::mutex device_mutex;BufferRef b{r.handle,r.object_generation,0,64,1,1};ResourceTable table(c,1,1,device_mutex),other(c,1,1,device_mutex);
 auto first=table.acquire(b,Consumer::CPU);auto second=other.acquire(b,Consumer::CPU);assert(first && second);
 assert(!table.release(*second.value));assert(table.release(*first.value));assert(other.release(*second.value));
 for(auto k:{Consumer::CPU,Consumer::DMA,Consumer::GPU,Consumer::TEX,Consumer::DP}){
  auto l=table.acquire(b,k);assert(l && l.value->token);assert(vxl_v2_buffer_release(&c,1,&r)==-EBUSY);
  auto corrupt=*l.value;corrupt.consumer=Consumer::DP==k?Consumer::CPU:Consumer::DP;assert(!table.release(corrupt));
  assert(table.release(*l.value));assert(!table.release(*l.value));
 }
 auto stale=b;stale.device_generation=2;assert(!table.acquire(stale,Consumer::GPU));
 std::atomic<unsigned> done{0};std::thread workers[4];
 for(auto& t:workers)t=std::thread([&]{for(unsigned i=0;i<1000;i++){auto l=table.acquire(b,Consumer::CPU);assert(l);assert(table.release(*l.value));done++;}});
 for(auto& t:workers)t.join();assert(done==4000);assert(vxl_v2_buffer_release(&c,1,&r)==0);
 std::cout<<"UNIFIED_LEASES_4_WORKER_LIFETIME_PASS_NOT_BOARD\n";
}
