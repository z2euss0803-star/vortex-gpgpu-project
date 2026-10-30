#include "io.hpp"
#include "native_origin.hpp"
#include "nnrt/package/json.hpp"
#include <vxl/demo_result.h>
#include <vxl/hunet.h>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace vxunified {namespace {
namespace j=vxnn::json;
using namespace result_io;
vxnn::Error error(int c,const char*s){return {c,"TEXTURE_IMPORT",s};}

}
Result<MosaicResultLease>result_io::import_at(int directory,const std::string&leaf){
 using R=Result<MosaicResultLease>;
 try{
  Fd root(dup(directory));j::check(root.value>=0,"result directory descriptor");Fd result_file(child(root.value,leaf));auto bytes=read(result_file.value,DEMO_MAX_JSON);MosaicResultLease result;result.original_json.assign(bytes.begin(),bytes.end());result.result_sha=hash(bytes);
  auto obj=j::parse(result.original_json);auto parsed=std::make_unique<demo_run>();j::check(!demo_parse_run(result.original_json.data(),result.original_json.size(),parsed.get()),"original result schema");j::check(parsed->app==DEMO_MOSAIC&&parsed->state==DEMO_RESULT_VALIDATED,"complete Mosaic result required");auto*o=obj.get();result.numeric_class=j::str(j::member(o,"numeric_class"));j::check(result.numeric_class!="FAIL","failed numeric result cannot become texture");const auto scope=j::str(j::member(o,"scope"));j::check(scope!="MOCK","mock output cannot become Mosaic texture");result.lease.scope=scope=="REAL_BOARD"?vxnn::EvidenceScope::REAL_BOARD:vxnn::EvidenceScope::OFFLINE;
  if(scope=="REAL_BOARD")j::check(parsed->cycle_valid&&parsed->raw_cycles,"real complete result requires original cycles");
  auto*shape=j::member(o,"shape");j::check(j::count(shape,4)==4&&j::u32(j::at(shape,0))==1&&j::u32(j::at(shape,1))==3,"CHW FP32 output shape");result.height=j::u32(j::at(shape,2));result.width=j::u32(j::at(shape,3));j::check(result.width&&result.width<=1920&&result.height&&result.height<=1080,"bounded image dimensions");uint64_t pixels=uint64_t(result.width)*result.height;
  auto*ids=j::member(o,"identities");result.lease.producer={j::str(j::member(o,"boot_id")),"mosaic-file",parsed->run_id};result.lease.app=AppKind::NN;result.lease.hardware=j::digest(j::member(ids,"hardware"));result.lease.model=j::digest(j::member(ids,"model"));result.lease.plan=result.result_sha;
  auto*rows=j::member(o,"artifacts");bool found=false;for(size_t i=0;i<j::count(rows,32);i++){
   auto*row=j::at(rows,i);auto role=j::str(j::member(row,"role"));auto name=j::str(j::member(row,"name"));parts(name);if(role!="output")continue;
   j::check(!found&&j::str(j::member(row,"dtype"))=="FP32","one complete FP32 result required");auto*asset_shape=j::member(row,"shape");j::check(j::count(asset_shape,4)==4,"output rank");for(unsigned k=0;k<4;k++)j::check(j::u32(j::at(asset_shape,k))==j::u32(j::at(shape,k)),"output shape differs");uint64_t n=j::u64(j::member(row,"bytes"));j::check(n==pixels*12&&n<=DEMO_MAX_ASSET,"output extent differs");result.tensor_sha=j::digest(j::member(row,"sha256"));Fd asset(child(root.value,name));result.raw_fp32=read(asset.value,DEMO_MAX_ASSET,n);j::check(hash(result.raw_fp32)==result.tensor_sha,"output SHA drift");result.lease.artifacts.push_back({name,"CHW_FP32_LE",result.tensor_sha,{1,3,result.height,result.width},n});found=true;
  }
  j::check(found,"FP32 output missing");result.rgb.resize(size_t(pixels)*3);for(uint64_t i=0;i<pixels;i++)for(unsigned c=0;c<3;c++){float v;std::memcpy(&v,result.raw_fp32.data()+4*(uint64_t(c)*pixels+i),4);j::check(std::isfinite(v),"nonfinite Mosaic output");result.rgb[3*i+c]=v<=0?0:v>=255?255:uint8_t(v);}result.rgb_sha=hash(result.rgb);
  if(auto origin=native_origin(obj)){result.lease.producer=origin->producer;result.lease.plan=origin->plan;result.lease.scope=origin->scope;}
  result.lease.artifacts.push_back({leaf,"MOSAIC_RESULT_JSON",result.result_sha,{bytes.size()},bytes.size()});return R::success(std::move(result));
 }catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"bounded source import allocation"));}catch(const std::exception&e){return R::failure({EINVAL,"TEXTURE_IMPORT",e.what()});}
}
Result<MosaicResultLease>import_result(const std::string&path){
 using R=Result<MosaicResultLease>;
 try{j::check(!path.empty()&&path.size()<=4096&&path.find('\0')==std::string::npos,"bounded result path");auto slash=path.find_last_of('/');auto parent=slash==std::string::npos?".":slash==0?"/":path.substr(0,slash);auto leaf=slash==std::string::npos?path:path.substr(slash+1);Fd root(open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC));j::check(root.value>=0,"result directory open");return result_io::import_at(root.value,leaf);}catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"source descriptor allocation"));}catch(const std::exception&e){return R::failure({EINVAL,"TEXTURE_IMPORT",e.what()});}
}
}
