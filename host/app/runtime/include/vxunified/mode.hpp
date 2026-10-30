#pragma once
#include <vxunified/types.hpp>
namespace vxunified {
struct ModeSnapshot {
 AppKind current=AppKind::NN,target=AppKind::NN;
 bool switching=false;
 uint64_t sequence=0,requested_ns=0,completed_ns=0;
};
// Metadata only. Runtime's backing mutex serializes these calls. A true
// drained argument comes from original completion/ACK/retirement accounting.
class SequentialModeGate {
 bool enabled_;ModeSnapshot state_;
public:
 explicit SequentialModeGate(bool enabled):enabled_(enabled){}
 Result<ModeSnapshot> request(AppKind,uint64_t now_ns);
 Result<bool> advance(bool drained,uint64_t now_ns);
 bool accepts(AppKind,bool existing_preparation=false)const;
 ModeSnapshot snapshot()const{return state_;}
};
}
