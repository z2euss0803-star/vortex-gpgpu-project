#pragma once
#include <vxunified/native_admission.hpp>
namespace vxunified {
// Compiled application bindings only. It owns neither another fd nor another
// Runtime queue/budget/pool; RPC cannot supply these sources or callbacks.
class NativeRouter {
 struct State;std::shared_ptr<State>state_;explicit NativeRouter(std::shared_ptr<State>);
public:
 static Result<std::shared_ptr<NativeRouter>>create(const StartupContext&);
 Result<WorkRequest>bind_source(WorkRequest,ExecutionHooks,std::shared_ptr<ResidentLease>,std::shared_ptr<const void>session_lifetime);
 ExecutionHooks handlers();
 size_t indexed()const;
 bool owns_runtime(const Runtime*)const;
 Result<uint32_t>allocate_cookie_namespace();
};
}
