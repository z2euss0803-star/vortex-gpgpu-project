#pragma once
#include <vxunified/service.hpp>
namespace vxunified {
struct RegisteredInput {std::string id;Digest sha{};uint64_t bytes=0;Digest plan{};};
// Internal, immutable startup registry, never supplied by an RPC peer. prepare
// lowers admitted software work only: no execution, ioctl submit or device open.
// Actual NN/graphics/image application registration is added by U11-U18.
struct TrustedApplication {
 std::string id;AppKind app=AppKind::NN;Digest hardware{},model{},plan{};
 std::vector<RegisteredInput> inputs;
 std::function<Result<WorkRequest>(const RpcRequest&)> prepare;
 std::function<Result<void>(const RpcRequest&)>validate;
 std::function<Result<RegisteredInput>(const std::string&)>resolve_input{};
 std::function<Result<std::vector<RegisteredInput>>()>list_inputs{};
 std::function<Result<RegisteredInput>(const RpcRequest&)>register_input{};
 // Read-only bounded metadata; callback must not reenter Runtime or perform I/O.
 std::function<Result<std::string>(const RequestKey&)>progress{};
};
using ResultControl=std::function<Result<std::string>(Runtime&,const RpcRequest&,const PublishedResult&)>;
struct PreparationReceipt {
 RequestKey request;BackendIdentity hardware;Digest model{},plan{},input{},source{};
 std::string entry,input_id;uint64_t begin_ns=0,end_ns=0;bool cancelled=false;
 std::optional<vxnn::Error>error;
};
using PersistPreparation=std::function<Result<void>(const PreparationReceipt&)>;
class ServiceControl {
 struct State;std::unique_ptr<State> state_;
 explicit ServiceControl(std::unique_ptr<State>);
public:
 ~ServiceControl();
 static Result<std::shared_ptr<ServiceControl>> start(std::shared_ptr<Runtime>,BackendIdentity,std::string boot,std::vector<TrustedApplication>,ResultControl display={},ResultControl export_result={},bool async_preparation=false,PersistPreparation={});
 Result<std::string> handle(const RpcRequest&);
 // Internal pump admission gate; cached identities still answer without prepare.
 Result<std::string> handle(const RpcRequest&,bool admission_open);
 Result<bool>prepare_one();
 Result<void>quiesce_preparations();
};
}
