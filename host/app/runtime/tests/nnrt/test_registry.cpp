#include "fixture.hpp"
int main(){OperatorRegistry r;auto s=identity_spec();require(bool(r.add(s)),"valid registry add");require(!r.add(s),"duplicate registry rejected");require(bool(r.resolve(s.key.name,s.key.semantic_version,s.key.implementation_sha)),"exact implementation resolved");Digest wrong{};require(!r.resolve(s.key.name,s.key.semantic_version,wrong),"wrong digest rejected");
 TensorDesc t;t.dtype=DType::FP32;t.layout="NCHW";require(bool(r.validate(s.key,{t},{},{})),"supported dtype layout");t.dtype=DType::I16;require(!r.validate(s.key,{t},{},{}),"bad dtype");t.dtype=DType::FP32;t.layout="NHWC";require(!r.validate(s.key,{t},{},{}),"bad layout");
 auto missing=s;missing.key.name="missing";missing.execute={};require(!r.add(missing),"incomplete registration rejected");
 puts("U07_REGISTRY_PASS_EXACT_KEY_OFFLINE_GPU_CYCLES_NULL");}
