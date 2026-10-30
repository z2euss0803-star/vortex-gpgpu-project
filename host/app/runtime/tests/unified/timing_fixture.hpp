#pragma once
#include <vxunified/evidence.hpp>
#include <cstdio>
#include <cstdlib>
namespace u19 {
inline void need(bool x,const char*s){if(!x){std::fprintf(stderr,"U19_TIMING_ASSERT %s\n",s);std::exit(1);}}
inline vxunified::Digest digest(unsigned n){vxunified::Digest d{};d[0]=uint8_t(n);return d;}
inline std::string hex(const vxunified::Digest&d){const char*h="0123456789abcdef";std::string s;for(auto b:d){s+=h[b>>4];s+=h[b&15];}return s;}
inline vxunified::EventContext context(){vxunified::EventContext c;c.hardware.hardware_contract=digest(1);c.hardware.bit=digest(2);c.hardware.xsa=digest(3);c.hardware.compatible=digest(4);c.hardware.device_generation=7;c.hardware.provider_flags=1;c.program={1,6,11,digest(5),digest(6),digest(1)};c.model=digest(7);c.plan=digest(8);c.source=digest(9);c.input=digest(10);c.request={"boot","client","req"};c.run_id="run";return c;}
inline vxunified::BoundEvent event(uint64_t id,uint64_t a,uint64_t b,vxunified::TimingPhase phase=vxunified::TimingPhase::END_TO_END){auto c=context();c.phase=phase;vxunified::Event e;e.event_id=id;e.run_id=c.run_id;e.boot_id=c.request.boot_id;e.request_id=c.request.request_id;e.model_id=hex(c.model);e.plan_id=hex(c.plan);e.source_id=hex(c.source);e.operator_id="conv0";e.job_id="job0";e.host="A";e.device_generation=7;e.start_ns=a;e.end_ns=b;return {e,c};}
}
