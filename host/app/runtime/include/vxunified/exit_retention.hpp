#pragma once
#include <atomic>
#include <memory>
namespace vxunified { namespace detail {
// Unknown retirement must remain anchored until OS process teardown. Embed the
// node while allocating normal state, not while handling an allocation failure.
// The intrusive root is intentional: neither exit nor static destruction guesses
// a device release, and leak diagnostics can trace all retained backing.
struct ExitRetention;
inline std::atomic<ExitRetention*> exit_retained_states{nullptr};
struct ExitRetention {
 std::shared_ptr<void> lifetime;
 ExitRetention*next=nullptr;
 template<class T>void retain(const std::shared_ptr<T>&state)noexcept{
  // Called on serialized owning shutdown/destruction paths; repeated failed
  // shutdown and destructor calls share one node. Control block conversion/
  // assignment and atomic insertion allocate nothing.
  if(lifetime)return;
  lifetime=state;
  next=exit_retained_states.load(std::memory_order_relaxed);
  while(!exit_retained_states.compare_exchange_weak(next,this,std::memory_order_release,std::memory_order_relaxed)){}
 }
};
}}
