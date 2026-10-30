#include <vxnn/request_input.hpp>
#include "scheduler_fixture.hpp"
#include <limits>
int main(){
 vxnn::Plan base;base.plan_id=digest(7);base.inputs={1};vxnn::PlannedTensor t;t.desc.id=1;t.desc.dtype=vxnn::DType::FP32;t.desc.layout="CHW_F32";t.desc.shape={3,128,128};t.desc.bytes=196608;base.tensors.push_back(t);
 vxnn::RequestInput input;input.id="example-roi_0";input.source_format="PNG";input.source_sha=digest(8);input.preprocessing="HUNET_P0_RGB_OTSU_AREA_PAD128_F32_V1";input.preprocessing_id=vxnn::content_digest(input.preprocessing);input.shape=t.desc.shape;
 auto content=std::make_shared<std::vector<uint8_t>>(196608);input.content=content;vxl_hunet_hash(content->data(),content->size(),input.tensor_sha.data());
 const auto old=vxnn::content_digest("NN_REQUEST_INPUT_V1:"+vxnn::digest_hex(base.plan_id)+":"+vxnn::digest_hex(input.source_sha)+":"+vxnn::digest_hex(input.tensor_sha)+":"+vxnn::digest_hex(input.preprocessing_id));
 auto actual=value(vxnn::bind_request_input(base,input),"original full-payload binder");require(actual.plan_id==old,"old literal identity bytes retained");
 auto different=input;different.source_sha[0]^=1;require(value(vxnn::bind_request_input(base,different),"independent source identity").plan_id!=old,"source identity changes plan");
 different=input;different.preprocessing="ANOTHER_APPROVED_POLICY_TEST_IDENTITY_ONLY";different.preprocessing_id=vxnn::content_digest(different.preprocessing);require(value(vxnn::bind_request_input(base,different),"independent policy identity").plan_id!=old,"policy identity changes plan");
 different=input;auto changed=std::make_shared<std::vector<uint8_t>>(*content);(*changed)[0]=1;different.content=changed;require(!vxnn::bind_request_input(base,different),"payload SHA mismatch rejected");vxl_hunet_hash(changed->data(),changed->size(),different.tensor_sha.data());require(value(vxnn::bind_request_input(base,different),"new matching tensor identity").plan_id!=old,"tensor identity changes plan");
 different=input;different.shape[0]++;require(!vxnn::bind_request_input(base,different),"shape mismatch rejected");different=input;different.content=std::make_shared<const std::vector<uint8_t>>(196604);require(!vxnn::bind_request_input(base,different),"short payload rejected");different=input;different.content.reset();require(!vxnn::bind_request_input(base,different),"metadata identity cannot admit missing content");
 changed=std::make_shared<std::vector<uint8_t>>(*content);float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(changed->data(),&nan,4);different=input;different.content=changed;vxl_hunet_hash(changed->data(),changed->size(),different.tensor_sha.data());require(!vxnn::bind_request_input(base,different),"finite input contract retained even with matching digest");
 std::puts("R5_HUNET_REQUEST_INPUT_IDENTITY_OLD_BYTES_PAYLOAD_SHAPE_FINITE_CONTRACT_PASS_HOST_NOT_GPU");
}
