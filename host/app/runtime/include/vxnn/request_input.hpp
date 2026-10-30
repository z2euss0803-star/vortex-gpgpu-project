#pragma once
#include <vxnn/graph.hpp>
#include <vxl/hunet.h>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <algorithm>
namespace vxnn {
// Request-owned immutable tensor. The model package and its frozen input asset
// are never replaced. SHA identities include source and preprocessing policy.
struct RequestInput {
 std::string id,source_format,preprocessing;
 Digest source_sha{},tensor_sha{},preprocessing_id{};
 DType dtype=DType::FP32;std::string layout="CHW_F32";Shape shape;
 std::shared_ptr<const std::vector<uint8_t>>content;
};
// Expected metadata identity only, never payload admission. The full binder
// below still checks bytes, SHA, shape, layout and finite values.
inline Digest request_input_plan_id(const Digest&base,const RequestInput&input){
 return content_digest("NN_REQUEST_INPUT_V1:"+digest_hex(base)+":"+digest_hex(input.source_sha)+":"+digest_hex(input.tensor_sha)+":"+digest_hex(input.preprocessing_id));
}
inline Result<Plan> bind_request_input(Plan plan,const RequestInput&input){
 using R=Result<Plan>;
 if(plan.inputs.size()!=1||!input.content||input.content->empty()||input.content->size()>(64u<<20)||!vxunified::nonzero_digest(input.source_sha)||!vxunified::nonzero_digest(input.tensor_sha)||input.preprocessing.empty()||input.preprocessing.size()>128||input.preprocessing_id!=content_digest(input.preprocessing))return R::failure({EINVAL,"REQUEST_INPUT","bounded identified input/policy required"});
 auto t=std::find_if(plan.tensors.begin(),plan.tensors.end(),[&](const PlannedTensor&t){return t.desc.id==plan.inputs[0];});
 if(t==plan.tensors.end()||t->desc.constant||t->first||t->desc.dtype!=DType::FP32||input.dtype!=t->desc.dtype||input.layout!=t->desc.layout||input.shape!=t->desc.shape||input.content->size()!=t->desc.bytes||input.content->size()%4)return R::failure({EINVAL,"REQUEST_INPUT","fixed declared FP32 input shape/layout/extent differs"});
 Digest digest{};vxl_hunet_hash(input.content->data(),input.content->size(),digest.data());if(digest!=input.tensor_sha)return R::failure({EBADMSG,"REQUEST_INPUT","actual tensor SHA differs"});
 for(size_t i=0;i<input.content->size();i+=4){float x;std::memcpy(&x,input.content->data()+i,4);if(!std::isfinite(x))return R::failure({EDOM,"REQUEST_INPUT","nonfinite tensor"});}
 plan.plan_id=request_input_plan_id(plan.plan_id,input);
 t->desc.content_sha=input.tensor_sha;
 return R::success(std::move(plan));
}
}
