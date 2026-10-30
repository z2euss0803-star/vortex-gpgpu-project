#include <vxunified/mode.hpp>
#include <cassert>
#include <cstdio>
using namespace vxunified;
int main(){
 SequentialModeGate gate(true);
 assert(gate.accepts(AppKind::NN));assert(!gate.accepts(AppKind::P8));
 auto first=gate.request(AppKind::P8,100);assert(first&&first.value->switching&&first.value->current==AppKind::NN);
 assert(!gate.accepts(AppKind::NN)&&!gate.accepts(AppKind::P8));
 assert(gate.accepts(AppKind::NN,true));assert(!gate.accepts(AppKind::P8,true));
 assert(!gate.request(AppKind::IMAGE,110));assert(gate.request(AppKind::P8,120));
 auto busy=gate.advance(false,130);assert(busy&&!*busy.value&&gate.snapshot().switching);
 assert(!gate.advance(true,99));auto done=gate.advance(true,140);assert(done&&*done.value);
 assert(gate.accepts(AppKind::P8)&&!gate.accepts(AppKind::NN));
 auto state=gate.snapshot();assert(state.sequence==1&&state.requested_ns==100&&state.completed_ns==140&&!state.switching);
 assert(gate.request(AppKind::IMAGE,200));assert(gate.advance(true,210));assert(gate.accepts(AppKind::IMAGE));
 assert(gate.request(AppKind::NN,300));assert(gate.advance(true,320));assert(gate.accepts(AppKind::NN)&&gate.snapshot().sequence==3);
 assert(!gate.request(AppKind(0),400)&&!gate.request(AppKind(4),400));
 SequentialModeGate old(false);assert(old.accepts(AppKind::NN)&&old.accepts(AppKind::P8)&&old.accepts(AppKind::IMAGE));assert(!old.request(AppKind::P8,100));
 puts("S8_JOINT_MODE_DRAIN_ADMISSION_CLOCK_IDEMPOTENCY_ROUNDTRIP_PASS_NOT_BOARD");
}
