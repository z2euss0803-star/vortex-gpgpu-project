#include "state.hpp"
namespace vxunified {
Result<void> Scheduler::cancel(RequestKey k){auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);auto it=s.index.find(State::key(k));if(it==s.index.end())return Result<void>::failure({ENOENT,"SCHEDULER","unknown request"});return s.cancel_locked(*it->second);}
Result<void> Scheduler::shutdown(){auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);s.closing=true;for(auto&kv:s.index){auto result=s.cancel_locked(*kv.second);if(!result)return result;}if(s.gpu||s.cpu)return Result<void>::failure({EBUSY,"SCHEDULER","active work retained until actual retirement"});return Result<void>::success();}
}
