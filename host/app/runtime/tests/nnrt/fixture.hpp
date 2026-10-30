#pragma once
#include <vxnn/graph.hpp>
#include <cassert>
#include <cstdio>
#include <cstdlib>
using namespace vxnn;
inline OperatorSpec identity_spec(){
 OperatorSpec s;s.key={"test.identity","1.0.0",{}};s.key.implementation_sha.fill(0xaa);s.available=true;s.supported_types={DType::FP32};s.supported_layouts={"NCHW"};
 s.infer_shapes=[](const auto&in,const auto&,const auto&){return Result<std::vector<Shape>>::success({in.at(0).shape});};
 s.workspace=[](const auto&,const auto&,const auto&){return Result<uint64_t>::success(0);};
 s.validate=[](const auto&in,const auto&,const auto&){return in.size()==1?Result<void>::success():Result<void>::failure({1,"fixture","arity"});};
 auto key=s.key;s.prepare=[key](NodeId id,const auto&in,const auto&out,const auto&params,const auto&){return Result<PreparedNode>::success({id,key,{in.at(0).id},{out.at(0).id},params,{}});};
 s.execute=[](const PreparedNode&,ExecutionContext&){return Result<Completion>::failure({1,"OFFLINE","not executed"});};return s;
}
inline DeviceCaps graph_caps(){DeviceCaps c;c.ps_bytes=1ULL<<20;c.pl_bytes=2ULL<<30;return c;}
inline OptimizationPolicy graph_policy(){OptimizationPolicy p;p.hardware.hardware_contract.fill(0x22);p.hardware.bit.fill(0x33);p.hardware.xsa.fill(0x44);p.hardware.compatible.fill(0x55);p.hardware.device_generation=1;p.hardware.provider_flags=1;return p;}
inline void require(bool b,const char* msg){if(!b){fprintf(stderr,"U07_ASSERT %s\n",msg);std::exit(41);}}
