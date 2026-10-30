#pragma once
#include <vxunified/daemon.hpp>
#include <vxunified/display.hpp>
namespace vxunified {
// Trusted execution-worker bridge. It shares the Runtime result store and the
// original single KMS owner; no RPC peer supplies the composer or stage journal.
class NativeDisplay {
 struct State;std::shared_ptr<State>state_;explicit NativeDisplay(std::shared_ptr<State>);
public:
 static Result<std::shared_ptr<NativeDisplay>>create(const StartupContext&,const dp_ops&,DisplayComposer,DisplayJournal,PersistResult,std::shared_ptr<void>lifetime={});
 static Result<std::shared_ptr<NativeDisplay>>create_native(const StartupContext&,DisplayComposer,DisplayJournal,PersistResult,const std::string&device="/dev/dri/card0");
 Result<DisplayReceipt>stage_and_present(const PublishedResult&,const DisplayLayout&,uint64_t logical_cookie=0);
 Result<DisplayReceipt>present_result(const PublishedResult&,const DisplayLayout&,uint64_t logical_cookie=0);
 Result<void>retire();
 DisplayObservation snapshot()const;
 dp_caps capabilities()const;
 bool owns_runtime(const Runtime*)const;
};
}
