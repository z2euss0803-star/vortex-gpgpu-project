#pragma once
#include <vxunified/native.hpp>
#include <vxunified/hunet_request.hpp>
#include <vxunified/mosaic_request.hpp>
#include <vxnn/ops_int16.hpp>
#include <vxunified/nn_input.hpp>
namespace vxunified {
class NativeFiles;
// Supplied by the sealed installation manifest, never by an RPC or by trusting
// a model package's own implementation claim. The package must match exactly.
struct InstalledNnPresets {std::string directory;Digest manifest{};};
struct InstalledNnModel {
 std::string id,input_id,package_directory;
 Digest model{},plan{},input{};uint64_t resident_bytes=0,input_bytes=0,job_budget_ns=0;
 std::variant<vxnn::HunetFamily,vxnn::MosaicFamily,vxnn::Int16Family>family;
 std::optional<InstalledNnPresets>presets;
};
class NativeNnLibrary:public std::enable_shared_from_this<NativeNnLibrary> {
 struct State;std::shared_ptr<State>state_;explicit NativeNnLibrary(std::shared_ptr<State>);
public:
 static Result<std::shared_ptr<NativeNnLibrary>>create(const StartupContext&,std::shared_ptr<NativeRouter>,std::shared_ptr<ProgramResidency>,const std::string&private_output_directory);
 static Result<std::shared_ptr<NativeNnLibrary>>create(const StartupContext&,std::shared_ptr<NativeRouter>,std::shared_ptr<ProgramResidency>,std::shared_ptr<NativeFiles>);
 Result<TrustedApplication>entry(const InstalledNnModel&);
 // Startup-owned directory, never an RPC path. Each registration reads only
 // <input_id>.f32, seals immutable host bytes, and preserves the frozen asset.
 Result<void>set_input_directory(const std::string&);
 Result<RegisteredInput>register_input(const std::string&entry_id,const NnInputRecord&);
 Result<std::vector<RegisteredInput>>list_inputs(const std::string&entry_id)const;
 Result<std::string>plan_inventory(const std::string&entry_id,const std::string&input_id)const;
 // Only after the common Runtime and all native sources have retired. Failure
 // keeps fixed metadata/model/input owners pinned for the original shutdown.
 Result<void>shutdown();
 Result<std::vector<uint8_t>>read_artifact(const ResultArtifact&)const;
};
}
