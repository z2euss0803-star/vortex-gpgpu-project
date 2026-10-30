#pragma once
#include <vxunified/scheduler.hpp>
#include <cstdio>
#include <cstdlib>
using namespace vxunified;
inline void require(bool b,const char*s){if(!b){std::fprintf(stderr,"U09_ASSERT %s\n",s);std::exit(1);}}
template<class T>inline T value(vxnn::Result<T> r,const char*s){if(!r)std::fprintf(stderr,"RESULT_ERROR %s code=%d category=%s detail=%s\n",s,r.error->code,r.error->category.c_str(),r.error->detail.c_str());require(bool(r),s);return *r.value;}
inline void ok(vxnn::Result<void> r,const char*s){if(!r)std::fprintf(stderr,"RESULT_ERROR %s code=%d category=%s detail=%s\n",s,r.error->code,r.error->category.c_str(),r.error->detail.c_str());require(bool(r),s);}
inline Digest digest(uint8_t v=1){Digest d{};d[0]=v;return d;}
struct Source:WorkSource {
 uint64_t count;WorkKind kind;bool cpu_first=false;mutable uint64_t reads=0;
 Source(uint64_t n,WorkKind k=WorkKind::GPU):count(n),kind(k){}
 uint64_t size()const override{return count;}
 Digest identity()const override{return digest(static_cast<uint8_t>(1+static_cast<int>(kind)));}
 vxnn::Result<WorkItem> get(uint64_t i)const override{++reads;WorkItem w;w.node=1;w.work_index=i;w.kind=cpu_first&&i==0?WorkKind::CPU:kind;if(i)w.dependencies={i-1};if(w.kind==WorkKind::GPU){GPUJobDesc j;j.cookie=i+1;j.program_handle=1;j.program_generation=1;j.device_generation=1;w.operation=j;}else w.operation=CpuTaskRef{"registered",1};return vxnn::Result<WorkItem>::success(w);}
};
inline WorkRequest request(const char*id,uint64_t n=2,WorkKind kind=WorkKind::GPU,AppKind app=AppKind::NN){WorkRequest r;r.request={"boot","client",id};r.app=app;r.model=digest(1);r.plan=digest(2);r.input_sha=digest(3);r.input_bytes=4;r.source=std::make_shared<Source>(n,kind);return r;}
inline WorkOutcome outcome(const WorkItem&w){WorkOutcome o;o.request=w.request;o.node=w.node;o.work_index=w.work_index;o.status=RequestStatus::COMPLETED;o.retired=true;o.acknowledged=true;if(w.kind==WorkKind::GPU){Completion c;c.sequence=w.work_index+1;c.cookie=std::get<GPUJobDesc>(w.operation).cookie;o.completion=c;}else if(w.kind==WorkKind::LEGACY_EXCLUSIVE){o.acknowledged=false;o.legacy.emplace();o.legacy->device_generation=1;o.legacy->profile_id=4;o.legacy->aggregate_complete=true;o.legacy->quiescent=true;o.legacy->result_sha=digest(8);o.legacy->info_sha=digest(9);}return o;}
