#include <vxunified/mode.hpp>
namespace vxunified {
Result<ModeSnapshot>SequentialModeGate::request(AppKind app,uint64_t now){
 if(!enabled_)return Result<ModeSnapshot>::failure({EOPNOTSUPP,"MODE","explicit S8 joint platform required"});
 if(!now||unsigned(app)<1||unsigned(app)>3)return Result<ModeSnapshot>::failure({EINVAL,"MODE","valid mode and monotonic timestamp required"});
 if(state_.switching){
  if(app!=state_.target)return Result<ModeSnapshot>::failure({EBUSY,"MODE","original mode handoff is still draining"});
  return Result<ModeSnapshot>::success(state_);
 }
 if(app==state_.current)return Result<ModeSnapshot>::success(state_);
 if(state_.sequence==UINT64_MAX||now<state_.completed_ns)return Result<ModeSnapshot>::failure({EOVERFLOW,"MODE","handoff sequence or clock exhausted"});
 state_.target=app;state_.switching=true;state_.requested_ns=now;state_.completed_ns=0;++state_.sequence;
 return Result<ModeSnapshot>::success(state_);
}
Result<bool>SequentialModeGate::advance(bool drained,uint64_t now){
 if(!enabled_||!state_.switching||!drained)return Result<bool>::success(false);
 if(!now||now<state_.requested_ns)return Result<bool>::failure({EPROTO,"MODE","monotonic handoff clock regressed"});
 state_.current=state_.target;state_.switching=false;state_.completed_ns=now;
 return Result<bool>::success(true);
}
bool SequentialModeGate::accepts(AppKind app,bool existing_preparation)const{
 return !enabled_||(app==state_.current&&(!state_.switching||existing_preparation));
}
}
