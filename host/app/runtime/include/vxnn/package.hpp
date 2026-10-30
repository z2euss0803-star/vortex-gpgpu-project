#pragma once
#include <vxnn/types.hpp>
#include <functional>
#include <map>
#include <memory>
#include <variant>
namespace vxnn {
struct PackageLimits {uint64_t max_total_bytes=1ULL<<30,max_json_bytes=256ULL<<10,max_evidence_bytes=1ULL<<30;uint32_t max_files=4096,max_jobs=0xfffffffeU;};
struct VerifiedAsset {std::string path,type,role;Digest sha{};std::shared_ptr<const std::vector<uint8_t>> content;};
class ModelPackage {
 struct Data;std::shared_ptr<const Data> data_;
 explicit ModelPackage(std::shared_ptr<const Data> p):data_(std::move(p)){}
 friend Result<ModelPackage> load_package(const Path&,const PackageLimits&);
public:
 const Digest& model_id()const;
 const std::map<std::string,VerifiedAsset>& assets()const;
 const std::string& graph_json()const;
 const std::string& program_requirements_json()const;
 const std::string& numeric_contract_json()const;
 uint64_t resident_bytes()const;uint32_t declared_max_jobs()const;uint64_t declared_max_evidence_bytes()const;
};
Result<ModelPackage> load_package(const Path&,const PackageLimits& = {});
Result<std::string> canonical_metadata(const std::string&,bool omit_model_id=false);
Digest content_digest(const std::string&);
std::string digest_hex(const Digest&);
enum class DType {FP32,I16,I48,U8,I32,FP16}; // Append only; FP16 is never signed I16.
struct TensorDesc {TensorId id=0;DType dtype=DType::FP32;std::string layout;Shape shape;uint64_t bytes=0,alignment=64;uint32_t access=0;AddressDomain domain=AddressDomain::PL;bool constant=false;std::string asset;Digest content_sha{};};
using Scalar=std::variant<bool,int64_t,uint64_t,std::string,std::vector<uint64_t>>;
using ScalarParams=std::map<std::string,Scalar>;
struct OperatorKey {std::string name,semantic_version;Digest implementation_sha{};bool operator<(const OperatorKey&)const;};
struct PreparedNode {NodeId id=0;OperatorKey key;std::vector<TensorId> inputs,outputs;ScalarParams parameters;std::vector<Digest> program_requirements;};
class ExecutionContext;
struct OperatorSpec {
 OperatorKey key;std::vector<DType> supported_types;std::vector<std::string> supported_layouts;bool available=false;
 std::function<Result<std::vector<Shape>>(const std::vector<TensorDesc>&,const ScalarParams&,const DeviceCaps&)> infer_shapes;
 std::function<Result<uint64_t>(const std::vector<TensorDesc>&,const ScalarParams&,const DeviceCaps&)> workspace;
 std::function<Result<void>(const std::vector<TensorDesc>&,const ScalarParams&,const DeviceCaps&)> validate;
 std::function<Result<PreparedNode>(NodeId,const std::vector<TensorDesc>&,const std::vector<TensorDesc>&,const ScalarParams&,const DeviceCaps&)> prepare;
 std::function<Result<Completion>(const PreparedNode&,ExecutionContext&)> execute;
};
class OperatorRegistry {
 std::map<OperatorKey,OperatorSpec> entries_;
public:
 Result<void> add(const OperatorSpec&);
 Result<OperatorSpec> resolve(const std::string&,const std::string&,const Digest&)const;
 Result<void> validate(const OperatorKey&,const std::vector<TensorDesc>&,const ScalarParams&,const DeviceCaps&)const;
};
}
