#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <cstdio>
#ifndef GN_PHASE_HEADER
#define GN_PHASE_HEADER "../../device/hunet_opt_gn_runtime.h"
#endif
#include GN_PHASE_HEADER
struct Shared {
 std::mutex mutex;std::condition_variable cv;unsigned arrived=0,generation=0;
 unsigned joins[4]={},fences[4]={},visits[2][64]={};int fail_core=-1;
};
struct Port {
 Shared &s;unsigned id;uint64_t tick=0;
 unsigned core(){return id;}
 void fence(){s.fences[id]++;}
 void barrier(uint32_t token,uint32_t cores){
  assert(token==0x80000000&&cores==4);assert(s.fences[id]==s.joins[id]+1);s.joins[id]++;
  std::unique_lock<std::mutex> lock(s.mutex);unsigned gen=s.generation;
  if(++s.arrived==4){s.arrived=0;s.generation++;s.cv.notify_all();}
  else if(!s.cv.wait_for(lock,std::chrono::seconds(2),[&]{return gen!=s.generation;}))throw std::runtime_error("GLOBAL_JOIN_MISSING");
 }
 uint64_t cycles(){return ++tick;}
 int spawn(OptGnRun *r,bool variance){
  if(int(id)==s.fail_core)return 1;
  for(uint32_t w=id*4;w<r->workers;w+=16)for(uint32_t lane=0;lane<4&&w+lane<r->workers;lane++){
   s.visits[variance?1:0][w+lane]++;opt_gn_worker(r,w+lane,variance);
  }
  return 0;
 }
};
static void run(uint32_t n,int fault){
 Shared shared;shared.fail_core=fault==3?1:-1;std::vector<float>x(n);for(uint32_t i=0;i<n;i++)x[i]=float(int(i%101)-50)/17;
 OptGnScratch scratch={};std::memset(scratch.guard_left,0xa5,64);std::memset(scratch.guard_right,0xa5,64);
 if(fault==1)scratch.guard_right[63]=0;
 if(fault==2)x[n/2]=NAN;
 if(fault==4)scratch.reserved[111]=1;
 float out[2]={123,456};int rc[4]={};HunetJobV1 job={};job.ci=1;job.ih=1;job.iw=n;job.epsilon_bits=0x3727c5ac;
 std::thread threads[4];for(unsigned c=0;c<4;c++)threads[c]=std::thread([&,c]{Port port{shared,c};rc[c]=opt_gn_phases(job,x.data(),out,&scratch,port);});
 for(auto &t:threads)t.join();
 for(unsigned c=0;c<4;c++){assert(shared.joins[c]==6&&shared.fences[c]==6);assert(rc[c]==rc[0]);}
 if(fault){assert(rc[0]&&out[0]==123&&out[1]==456);return;}
 assert(!rc[0]);float partial[64]={},variance[64]={};uint32_t w=opt_gn_workers(n);
 for(uint32_t i=0;i<w;i++){assert(shared.visits[0][i]==1&&shared.visits[1][i]==1);partial[i]=opt_gn_partial(x.data(),opt_gn_range(n,i,w),0,false);}
 float mean=opt_gn_merge(partial,w)/n;
 for(uint32_t i=0;i<w;i++)variance[i]=opt_gn_partial(x.data(),opt_gn_range(n,i,w),mean,true);
 assert(out[0]==mean&&out[1]==opt_gn_invstd(opt_gn_merge(variance,w),n,1e-5f));
}
int main(){run(1,0);run(4097,0);run(65539,0);for(int f=1;f<=4;f++)run(65539,f);puts("HOST_ACTUAL_GN_PHASES_ALL_FOUR_CORES_JOIN_LEADER_ORDER_FAILURES_PASS_NOT_GPU");return 0;}
