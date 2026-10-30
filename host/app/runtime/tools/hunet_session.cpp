#include "hunet_session.h"
#include "hunet_timing.h"
#include <vxl/hunet.h>
#include "../generated/hunet/layout.h"
#include "../generated/hunet/stages.h"
#include <cstdio>
static int capture(vxl_hunet_client *c,uint64_t gpu,uint64_t bytes,std::vector<uint8_t> &out){out.resize(bytes);return vxl_hunet_client_copy(c,c->handles[2],gpu-VXL_HUNET_WS,out.data(),bytes,0);}
static uint64_t remaining_ms(uint64_t deadline){uint64_t now=hunet_now();return now>=deadline?0:(deadline-now)/1000000ULL;}
int hunet_execute(vxl_hunet_client *c,const vxl_hunet_begin_v1 &b,bool diag,HunetRaw *r){
 uint64_t begin=hunet_now(),deadline,start,finish,id=0,budget;int rc;
 if(begin>UINT64_MAX-uint64_t(VXL_HUNET_NETWORK_MS)*1000000ULL){rc=-EOVERFLOW;goto fail;}
 deadline=begin+uint64_t(VXL_HUNET_NETWORK_MS)*1000000ULL;rc=vxl_hunet_client_begin(c,&b);if(rc)goto fail;
 for(unsigned seq=1;seq<=1023;seq++){
  vxl_hunet_job_v1 j{};vxl_hunet_job_evidence_v1 e{};memcpy(&j,vxl_hunet_catalog_job(seq),128);
  if(!remaining_ms(deadline)){rc=-ETIMEDOUT;goto fail;}
  start=hunet_now();r->jobs.push_back(j);rc=vxl_hunet_client_submit(c,&j,&id);if(rc){r->submit_wait_ns.push_back(hunet_duration(start,hunet_now()));goto fail;}
  budget=remaining_ms(deadline);if(!budget){rc=-ETIMEDOUT;goto fail;}if(budget>60000)budget=60000;
  rc=vxl_hunet_client_wait(c,id,budget);finish=hunet_now();r->submit_wait_ns.push_back(hunet_duration(start,finish));if(rc)goto fail;
  rc=vxl_hunet_client_evidence(c,id,&e);if(rc)goto fail;r->evidence.push_back(e);
  if(e.software_state!=VXL_COMPLETED||e.result||e.sequence!=seq||e.job_id!=id){rc=-EIO;goto fail;}
  if(!remaining_ms(deadline)){rc=-ETIMEDOUT;goto fail;}
  if(diag){
   uint64_t cb=hunet_now();char name[80];
   if(j.op==2){HunetRaw::Capture a;snprintf(name,sizeof(name),"traces/%04u.u32",seq);a.name=name;rc=capture(c,j.scratch,uint64_t(j.tasks)*4,a.bytes);if(rc)goto fail;r->diagnostic.push_back(std::move(a));}
   if(seq==vxl_hunet_stages[j.stage_id].last_sequence){HunetRaw::Capture a;snprintf(name,sizeof(name),"stages/%03u.f32",j.stage_id);a.name=name;rc=capture(c,vxl_hunet_stages[j.stage_id].gpu,vxl_hunet_stages[j.stage_id].bytes,a.bytes);if(rc)goto fail;r->diagnostic.push_back(std::move(a));}
   r->diagnostic_capture_ns+=hunet_duration(cb,hunet_now());
  }
  rc=vxl_hunet_client_reap(c,id);if(rc)goto fail;
 }
 if(!remaining_ms(deadline)){rc=-ETIMEDOUT;goto fail;}
 rc=capture(c,VXL_HUNET_LOGITS,32420,r->logits);if(rc)goto fail;
 rc=capture(c,VXL_HUNET_INPUT,196608,r->input);if(rc)goto fail;
 rc=capture(c,0x87044080,1014*4,r->trace);if(rc)goto fail;
 for(unsigned i=0;i<106;i++)for(unsigned side=0;side<2;side++){
  std::vector<uint8_t> g;rc=capture(c,side?vxl_hunet_slots[i].start+vxl_hunet_slots[i].bytes:vxl_hunet_slots[i].start-64,64,g);if(rc)goto fail;r->guards.insert(r->guards.end(),g.begin(),g.end());
 }
 if(!remaining_ms(deadline)){rc=-ETIMEDOUT;goto fail;}
 rc=vxl_hunet_client_end(c,&r->end);if(rc)goto fail;
 r->resident_ns=hunet_duration(begin,hunet_now());return 0;
fail:
 r->error=rc;c->stopped=1;
 if(c->last_job){if(rc==-EFAULT&&begin<=UINT64_MAX-uint64_t(VXL_HUNET_NETWORK_MS)*1000000ULL){budget=remaining_ms(begin+uint64_t(VXL_HUNET_NETWORK_MS)*1000000ULL);if(budget>60000)budget=60000;if(budget)vxl_hunet_client_wait(c,c->last_job,budget);}vxl_hunet_client_evidence(c,c->last_job,&r->failed_evidence);}
 r->resident_ns=hunet_duration(begin,hunet_now());return rc;
}
