#pragma once
#include <vxnn/package.hpp>
#include <json-c/json.h>
#include <algorithm>
#include <cerrno>
#include <limits>
#include <set>
#include <stdexcept>
namespace vxnn {namespace json {
using Ptr=std::shared_ptr<json_object>;
inline void check(bool b,const std::string&s){if(!b)throw std::runtime_error(s);}
Ptr parse(const std::string&);
inline json_object* member(json_object*o,const char*k){json_object*v=nullptr;check(o&&json_object_is_type(o,json_type_object)&&json_object_object_get_ex(o,k,&v),std::string("missing field ")+k);return v;}
inline bool has(json_object*o,const char*k){json_object*v=nullptr;return json_object_object_get_ex(o,k,&v);}
inline void fields(json_object*o,std::initializer_list<const char*> allowed){check(o&&json_object_is_type(o,json_type_object),"expected object");std::set<std::string> names;for(auto k:allowed)names.insert(k);json_object_object_foreach(o,k,v){(void)v;check(names.count(k)!=0,std::string("unknown field ")+k);}}
inline std::string str(json_object*v){check(v&&json_object_is_type(v,json_type_string),"expected string");return std::string(json_object_get_string(v),json_object_get_string_len(v));}
inline uint64_t u64(json_object*v){check(v&&json_object_is_type(v,json_type_int),"expected integer");check(json_object_get_string(v)[0]!='-',"negative unsigned integer");return json_object_get_uint64(v);}
inline uint32_t u32(json_object*v){auto n=u64(v);check(n<=UINT32_MAX,"u32 overflow");return uint32_t(n);}
inline bool boolean(json_object*v){check(v&&json_object_is_type(v,json_type_boolean),"expected boolean");return json_object_get_boolean(v);}
inline size_t count(json_object*v,size_t limit=4096){check(v&&json_object_is_type(v,json_type_array),"expected array");auto n=json_object_array_length(v);check(n<=limit,"array limit");return n;}
inline json_object* at(json_object*v,size_t n){return json_object_array_get_idx(v,n);}
inline Digest digest(json_object*v){auto s=str(v);check(s.size()==64,"digest length");Digest d{};for(size_t i=0;i<32;i++){auto h=[](char c)->unsigned{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("digest encoding");};d[i]=uint8_t((h(s[2*i])<<4)|h(s[2*i+1]));}return d;}
inline uint64_t add(uint64_t a,uint64_t b){check(a<=UINT64_MAX-b,"addition overflow");return a+b;}
inline uint64_t mul(uint64_t a,uint64_t b){check(!b||a<=UINT64_MAX/b,"multiplication overflow");return a*b;}
inline uint64_t align(uint64_t a,uint64_t b){check(b&&!(b&(b-1)),"invalid alignment");return add(a,b-1)&~(b-1);}
inline std::string dump(json_object*v){check(v!=nullptr,"null not permitted here");return json_object_to_json_string_ext(v,JSON_C_TO_STRING_PLAIN|JSON_C_TO_STRING_NOSLASHESCAPE);}
}}
