#pragma once
#include <vxunified/display.hpp>
#include <vxl/hunet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
using namespace vxunified;
inline void need(bool x,const char*s){if(!x){std::fprintf(stderr,"U18_DISPLAY_ASSERT %s\n",s);std::exit(1);}}
template<class T>T get(const Result<T>&r,const char*s){if(!r&&r.error)std::fprintf(stderr,"U18_ACTUAL_ERROR code=%d category=%s detail=%s\n",r.error->code,r.error->category.c_str(),r.error->detail.c_str());need(bool(r),s);return *r.value;}
inline void good(const Result<void>&r,const char*s){need(bool(r),s);}
inline Digest sum(const std::vector<uint8_t>&b){Digest d{};vxl_hunet_hash(b.data(),b.size(),d.data());return d;}
struct FakeDisplay {
 int opened=0,created=0,destroyed=0,closed=0,disabled=0,scan=0,next=0;
 int receive_error=0,restore_error=0;uint64_t cookie=0,event_cookie=0;
 uint8_t*maps[2]{};
 static int open_op(void*p,const dp_config*,dp_caps*c){auto&f=*static_cast<FakeDisplay*>(p);++f.opened;*c={4,2,1,1,1,0};return 0;}
 static int create_op(void*p,uint32_t w,uint32_t h,dp_buffer*b){auto&f=*static_cast<FakeDisplay*>(p);int i=f.created++;f.maps[i]=static_cast<uint8_t*>(std::calloc(1,48));*b={uint32_t(i+1),w,h,24,DP_ARGB8888,48,f.maps[i]};return 0;}
 static int initial_op(void*p,const dp_buffer*b){static_cast<FakeDisplay*>(p)->scan=b->id;return 0;}
 static int flip_op(void*p,const dp_buffer*b,uint64_t c){auto&f=*static_cast<FakeDisplay*>(p);f.next=b->id;f.cookie=c;return 0;}
 static int receive_op(void*p,int,uint64_t*c,uint64_t*t){auto&f=*static_cast<FakeDisplay*>(p);if(f.receive_error)return f.receive_error;*c=f.event_cookie?f.event_cookie:f.cookie;*t=1234;f.scan=f.next;f.next=0;return 0;}
 static int disable_op(void*p){auto&f=*static_cast<FakeDisplay*>(p);++f.disabled;if(f.restore_error)return f.restore_error;f.scan=f.next=0;return 0;}
 static void destroy_op(void*p,dp_buffer*b){auto&f=*static_cast<FakeDisplay*>(p);need(f.scan!=int(b->id)&&f.next!=int(b->id),"live scanout cannot be destroyed");++f.destroyed;std::free(b->map);f.maps[b->id-1]=nullptr;}
 static void close_op(void*p){++static_cast<FakeDisplay*>(p)->closed;}
 dp_ops ops(){return {open_op,create_op,initial_op,flip_op,receive_op,disable_op,destroy_op,close_op,this};}
};
inline std::shared_ptr<const DisplayCanvas> canvas(){auto c=std::make_shared<DisplayCanvas>();c->width=4;c->height=2;c->rgb={255,17,4, 0,255,0, 0,0,255, 7,8,9, 1,2,3, 4,5,6, 7,8,9, 10,11,12};c->pixel_sha=sum(c->rgb);c->source_artifact=c->pixel_sha;return c;}
inline PublishedResult producer(const char*id="one"){
 PublishedResult p;p.lease.producer={"boot","client",id};p.lease.app=AppKind::IMAGE;p.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;p.lease.hardware[0]=1;p.lease.model[0]=2;p.lease.plan[0]=3;
 ResultArtifact a;a.path="result.rgb";a.layout="RGB888";a.sha=canvas()->pixel_sha;a.shape={2,4,3};a.bytes=24;p.lease.artifacts={a};p.execution_complete=true;p.numeric=vxnn::NumericClass::KNOWN_NUMERIC_DEVIATION;p.raw_legacy_json="{\"historical\":\"failure preserved\"}";return p;
}
inline DisplayLayout layout(){return {4,2,4,2,0,0,4,2,PixelFormat::RGBA8888};}
inline DisplayComposer composer(){return [](const PublishedResult&,const DisplayLayout&){return Result<std::shared_ptr<const DisplayCanvas>>::success(canvas());};}
inline DisplayJournal journal(){return [](const DisplayObservation&){return Result<void>::success();};}
inline std::shared_ptr<ResultStore> store(int&retired){return std::make_shared<ResultStore>([](const PublishedResult&){return Result<void>::success();},[&retired](const ResultLease&){++retired;return Result<void>::success();});}
