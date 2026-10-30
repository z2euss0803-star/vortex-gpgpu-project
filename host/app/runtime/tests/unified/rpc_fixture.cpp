// This test executable alone registers a host protocol fixture. Production
// vxgpud must never expose a fixture mode or open a device from client input.
#include <vxunified/service.hpp>
#include <vxunified/scheduler.hpp>
#include "scheduler_fixture.hpp"
#include <cstdio>
#include <thread>
#include <chrono>
using namespace vxunified;
int main(int argc,char**argv){
 if(argc!=2){return 2;}Scheduler scheduler;unsigned control_calls=0;
 auto server=RpcServer::start(argv[1],"boot",[&](const RpcRequest&r)->Result<std::string>{
  ++control_calls;
  // CPU-only handler duration intentionally exceeds the200ms transport window.
  // Production never registers this fixture destination or any device work.
  if(r.command=="export"&&r.destination_id=="host-slow-file-check"){std::this_thread::sleep_for(std::chrono::milliseconds(650));return Result<std::string>::success("{\"checked\":true,\"gpu_raw_cycles\":null}");}
  if(r.command=="models"||r.command=="scenes"||r.command=="effects")return Result<std::string>::success("{\"entries\":[],\"control_calls\":"+std::to_string(control_calls)+",\"gpu_raw_cycles\":null}");
  if(r.command=="submit"){
   if(r.entry_id!="fixture"||r.input_id!="fixed")return Result<std::string>::failure({ENOENT,"ADMISSION","unregistered entry/input"});
   auto req=request(r.request.request_id.c_str(),1,WorkKind::CPU);req.request=r.request;req.input_sha[0]=r.parameters=="{}"?1:2;
   auto q=scheduler.submit(req);if(!q)return Result<std::string>::failure(*q.error);return Result<std::string>::success("{\"status\":\"QUEUED\"}");
  }
  if(r.command=="cancel"){auto c=scheduler.cancel(r.request);if(!c)return Result<std::string>::failure(*c.error);}
  auto s=scheduler.status(r.request);if(!s)return Result<std::string>::failure(*s.error);
  if(r.command=="status"||r.command=="cancel")return Result<std::string>::success("{\"status\":\""+std::string(*s.value==RequestStatus::QUEUED?"QUEUED":*s.value==RequestStatus::CANCELLED?"CANCELLED":"OTHER")+"\",\"gpu_raw_cycles\":null}");
  return Result<std::string>::failure({EOPNOTSUPP,"FIXTURE","no result or presentation in protocol fixture"});
 });
 if(!server){fprintf(stderr,"U10_RPC_ASSERT start failed code=%d\n",server.error->code);return 1;}
 puts("U10_RPC_READY");fflush(stdout);
 for(;;){auto r=(*server.value)->serve_one(200);if(!r&&r.error->code!=EAGAIN&&r.error->code!=ETIMEDOUT&&r.error->category!="RPC_PEER"){fprintf(stderr,"U10_RPC_SERVER_ERROR %d %s\n",r.error->code,r.error->detail.c_str());return 1;}}
}
