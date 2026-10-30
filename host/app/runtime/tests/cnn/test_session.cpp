#include "../../tools/cnn_session.h"
extern "C" {
#include "../support/strict_backend.h"
}
#include <cassert>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <cerrno>
#include <chrono>
#include <vxl/cnn.h>
struct Backend {
 strict_backend s{};CnnOutputs golden{};std::vector<uint32_t> calls,ids;
 unsigned closes=0,waits=0;int fail_cmd=0,close_error=0;bool bad_value=false,bad_guard=false,fail_args_copy=false;
};
static vxl_object *find(Backend *b,uint32_t addr){for(auto &o:b->s.engine.objects)if(o.handle&&o.gpu+64==addr)return &o;return nullptr;}
static int call(void *ctx,uint32_t cmd,vxl_request *r){
 auto *b=static_cast<Backend*>(ctx);b->calls.push_back(cmd);
 if(b->fail_args_copy&&cmd==VXL_COPY_TO&&r->handle==b->s.engine.objects[1].handle)return -EIO;
 if(cmd==VXL_COPY_TO){
  for(unsigned index:{5U,6U,7U,10U})if(b->s.engine.objects[index].handle==r->handle){
   auto words=reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(r->user_ptr));
   const unsigned n=index==5||index==6?72:index==7?18:2;
   for(unsigned j=0;j<n;j++)assert(words[16+j]==VXL_CNN_POISON);
  }
 }
 if(static_cast<int>(cmd)==b->fail_cmd)return -EIO;
 if(cmd==VXL_WAIT){b->waits++;strict_complete(&b->s);}
 int rc=vxl_engine_call(&b->s.engine,1,cmd,r);
 if(!rc&&cmd==VXL_SUBMIT){
  auto p=b->s.engine.prepared;unsigned id=p.program_id;b->ids.push_back(id);
  if(id==5){auto *in=find(b,p.args_words[4]);assert(in);assert(!memcmp(static_cast<char*>(in->storage)+64,b->golden.p,72));}
  auto *out=find(b,p.args_words[7]);assert(out);
  const int32_t *gold=id==2?b->golden.z:id==3?b->golden.r:id==4?b->golden.p:b->golden.y;
  memcpy(static_cast<char*>(out->storage)+64,gold,p.args_words[11]*4);
  if(b->bad_value&&id==2)static_cast<uint32_t*>(out->storage)[16]^=1;
  if(b->bad_guard&&id==2)static_cast<uint32_t*>(out->storage)[0]^=1;
 }
 return rc;
}
static int close(void *ctx){auto *b=static_cast<Backend*>(ctx);b->closes++;vxl_engine_close(&b->s.engine,1);return b->close_error;}
struct Clock {uint64_t ns=UINT64_C(0x200000000);unsigned calls=0,fail_at=0;};
static int now(void *ctx,uint64_t *n){auto *c=static_cast<Clock*>(ctx);if(++c->calls==c->fail_at)return -EIO;*n=c->ns;c->ns+=100;return 0;}
static void init(Backend &b){strict_init(&b.s);assert(!vxl_engine_open(&b.s.engine,1));int32_t x[64];for(unsigned i=0;i<64;i++)x[i]=(5*i+1)%9-4;assert(!cnn_reference(x,cnn_fixed_weights(),&b.golden));}
int main(int argc,char **argv){
 assert(argc==2);Backend b;init(b);Clock clock;CnnSession s(CnnIo{&b,call,close});CnnRun run;run.now=now;run.clock_ctx=&clock;
 assert(!cnn_run_suite(s,cnn_fixed_weights(),&run));
 assert(run.iterations.size()==24&&b.ids.size()==96&&b.closes==1&&!b.s.allocations);
 assert(run.final_info_valid&&run.final_info.state==VXL_READY&&run.final_info.used==0);
 for(unsigned i=0;i<96;i++)assert(b.ids[i]==i%4+2);
 for(size_t i=2;i+4<b.calls.size();i++)if(b.calls[i]==VXL_SUBMIT){
  assert(b.calls[i-1]==VXL_COPY_TO&&b.calls[i-2]==VXL_COPY_TO);
  assert(b.calls[i+1]==VXL_WAIT&&b.calls[i+2]==VXL_GET_STATUS&&b.calls[i+3]==VXL_REAP&&b.calls[i+4]==VXL_COPY_FROM);
 }
 for(unsigned i=0;i<24;i++){
  const auto &r=run.iterations[i];assert(r.result==0&&r.audit.valid&&r.kernel_chain_host_ns==400);
  assert(r.phase==(i==0?"FIRST":i<4?"WARMUP":"MEASURED"));
  for(auto &l:r.layers)assert(l.snapshot_valid&&l.guard_pass&&!l.mismatch_count&&l.snapshot.hwstatus==2);
 }
 /* Save protocol records for the independent Python summary, never board timing. */
 std::string evidence=std::string(argv[1])+"/"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 std::filesystem::create_directories(evidence);assert(!cnn_save_run(evidence,&run));
 std::ofstream trace(evidence+"/calls.txt");for(auto c:b.calls)trace<<c<<'\n';trace.close();assert(trace);
 assert(s.close()==0&&b.closes==1);
 for(int mode=0;mode<10;mode++){
  Backend f;init(f);Clock c;CnnSession ss(CnnIo{&f,call,close});CnnRun rr;rr.now=now;rr.clock_ctx=&c;
  if(mode==0)f.bad_value=true;
  if(mode==1)f.bad_guard=true;
  if(mode==2)f.fail_cmd=VXL_SUBMIT;
  if(mode==3)f.fail_cmd=VXL_WAIT;
  if(mode==4)f.fail_cmd=VXL_REAP;
  if(mode==5)f.fail_cmd=VXL_COPY_FROM;
  if(mode==6)c.fail_at=7;
  if(mode==7)f.close_error=-EINTR;
  if(mode==8)f.s.completion_error=1;
  if(mode==9)f.fail_args_copy=true;
  assert(cnn_run_suite(ss,cnn_fixed_weights(),&rr)<0);
  assert(rr.result<0&&f.closes==1);
  if(mode!=7)assert(f.ids.size()<=1);
  if(mode==3||mode==8)assert(rr.pinned_retained&&f.s.allocations==11);
  if(mode==0||mode==1)assert(rr.iterations.size()==1&&rr.iterations[0].layers[0].raw[3].size());
  if(mode==9)assert(rr.iterations[0].layers[0].code_h2d_bytes==4096&&rr.iterations[0].layers[0].args_h2d_bytes==0);
  assert(ss.close()==rr.close_result&&f.closes==1);strict_dispose(&f.s);
 }
 assert(cnn_save_run(std::string(argv[1])+"/missing/dir",&run)<0&&run.save_result<0);
 puts("TEST_PROTOCOL_NOT_GPU_96_JOBS_FAILURE_STOP_AND_CLOSE_PASS");
}
