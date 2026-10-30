#include "fixture.hpp"
#include <cstring>
int main(int argc,char**argv){
 require(argc==2,"fixture directory");
 static_assert(int(DType::FP32)==0&&int(DType::I16)==1&&int(DType::I48)==2&&int(DType::U8)==3&&int(DType::I32)==4,"old types retain ordinals");
 const auto half=static_cast<DType>(5); // Compilable functional RED before enum extension.
 auto spec=identity_spec();spec.key.name="test.fp16.storage";spec.supported_types={half};spec.supported_layouts={"RM_FP16"};
 spec.validate=[half](const auto&in,const auto&,const auto&){return in.size()==1&&in[0].dtype==half&&in[0].bytes==16&&in[0].constant?Result<void>::success():Result<void>::failure({EINVAL,"FP16","explicit half dtype/storage required"});};
 auto key=spec.key;spec.prepare=[key](NodeId id,const auto&in,const auto&out,const auto&p,const auto&){return Result<PreparedNode>::success({id,key,{in.at(0).id},{out.at(0).id},p,{}});};
 OperatorRegistry registry;require(bool(registry.add(spec)),"FP16 test registry");
 auto prepare=[&](const char*name){auto p=load_package(Path(argv[1])/name);require(bool(p),"valid immutable fixture including semantic negatives");return prepare_graph(*p.value,registry,graph_caps(),graph_policy());};
 auto plan=prepare("fp16");if(!plan)std::fprintf(stderr,"U12_FP16_ERROR %s\n",plan.error->detail.c_str());require(bool(plan),"FP16 weight/input/output representation supported without I16 relabel");
 for(const auto&t:plan.value->tensors)require(t.desc.dtype==half&&t.desc.bytes==16&&t.desc.shape==Shape{2,4},"each half element uses two bytes");
 require(plan.value->inputs.empty()&&plan.value->nodes.size()==1,"constant half weights retain graph residency");
 for(const char*name:{"i16-mislabel","wrong-layout","short-constant"})require(!prepare(name),"bad type/layout/constant extent rejected");
 puts("U12_FP16_STORAGE_PLAN_PASS_LEGACY_TYPES_UNCHANGED_NOT_GPU");
}
