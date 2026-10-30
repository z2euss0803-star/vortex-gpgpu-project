#include "json.hpp"
#include <vxl/hunet.h>
namespace vxnn {namespace {
// json-c validates UTF8/escapes. This bounded lexical pass additionally rejects
// duplicate decoded keys, floats, extensions and integer saturation *before*
// json-c can replace an object member or clamp an integer.
class Canonical {
 const std::string&s;size_t pos=0,tokens=0;bool omit;
 void space(){while(pos<s.size()&&(s[pos]==' '||s[pos]=='\n'||s[pos]=='\r'||s[pos]=='\t'))++pos;}
 char peek(){space();json::check(pos<s.size(),"truncated JSON");return s[pos];}
 std::pair<std::string,std::string> string(){
  json::check(peek()=='"',"expected string token");size_t start=pos++;bool end=false;
  while(pos<s.size()){char c=s[pos++];if(c=='"'){end=true;break;}if(c=='\\'){json::check(pos<s.size(),"escape truncated");++pos;}}
  json::check(end,"string truncated");auto raw=s.substr(start,pos-start);auto*t=json_tokener_new_ex(32);json::check(t!=nullptr,"JSON allocation");json_tokener_set_flags(t,JSON_TOKENER_STRICT|JSON_TOKENER_VALIDATE_UTF8);auto*v=json_tokener_parse_ex(t,raw.c_str(),int(raw.size()));bool ok=json_tokener_get_error(t)==json_tokener_success;json_tokener_free(t);json::Ptr obj(v,json_object_put);json::check(ok&&v&&json_object_is_type(v,json_type_string),"invalid UTF8/string");auto value=json::str(v);json::check(value.find('\0')==std::string::npos,"NUL string");return {value,json::dump(v)};
 }
 std::string value(unsigned depth){
  json::check(depth<=32&&++tokens<=65536,"JSON complexity limit");char c=peek();
  if(c=='"')return string().second;
  if(c=='{'){
   ++pos;std::map<std::string,std::pair<std::string,std::string>> members;
   if(peek()!='}')while(true){auto key=string();json::check(peek()==':',"object colon");++pos;auto v=value(depth+1);json::check(members.emplace(key.first,std::make_pair(key.second,v)).second,"duplicate JSON key");char p=peek();if(p=='}')break;json::check(p==',',"object comma");++pos;}
   ++pos;std::string out="{";for(auto&kv:members){if(omit&&depth==0&&kv.first=="model_id")continue;if(out.size()>1)out+=',';out+=kv.second.first+":"+kv.second.second;}return out+"}";
  }
  if(c=='['){++pos;std::string out="[";if(peek()!=']')while(true){if(out.size()>1)out+=',';out+=value(depth+1);char p=peek();if(p==']')break;json::check(p==',',"array comma");++pos;}++pos;return out+"]";}
  for(auto literal:{"true","false","null"}){auto n=std::char_traits<char>::length(literal);if(s.compare(pos,n,literal)==0){pos+=n;return literal;}}
  bool neg=c=='-';if(neg)++pos;size_t first=pos;uint64_t number=0,limit=neg?(1ULL<<63):UINT64_MAX;
  while(pos<s.size()&&s[pos]>='0'&&s[pos]<='9'){unsigned digit=s[pos++]-'0';json::check(number<=limit/10&&(number<limit/10||digit<=limit%10),"integer overflow");number=number*10+digit;}
  json::check(pos>first&&!(pos-first>1&&s[first]=='0'),"invalid integer");json::check(pos==s.size()||(s[pos]!='.'&&s[pos]!='e'&&s[pos]!='E'),"floating metadata forbidden");return (neg&&number?"-":"")+std::to_string(number);
 }
public:
 Canonical(const std::string&in,bool o):s(in),omit(o){}
 std::string run(){json::check(!s.empty()&&s.size()<=(256U<<10),"JSON byte limit");auto out=value(0);space();json::check(pos==s.size(),"trailing JSON");return out;}
};
}
Result<std::string> canonical_metadata(const std::string&s,bool omit){try{return Result<std::string>::success(Canonical(s,omit).run());}catch(const std::exception&e){return Result<std::string>::failure({EINVAL,"PACKAGE_REJECTED",e.what()});}}
Digest content_digest(const std::string&s){Digest d{};vxl_hunet_hash(s.data(),s.size(),d.data());return d;}
std::string digest_hex(const Digest&d){static const char hex[]="0123456789abcdef";std::string s;for(auto c:d){s+=hex[c>>4];s+=hex[c&15];}return s;}
namespace json {
Ptr parse(const std::string&s){auto canonical=canonical_metadata(s);check(bool(canonical),canonical.error?canonical.error->detail:"JSON rejected");auto raw=*canonical.value+" ";auto*t=json_tokener_new_ex(32);check(t!=nullptr,"JSON allocation");json_tokener_set_flags(t,JSON_TOKENER_STRICT|JSON_TOKENER_VALIDATE_UTF8);auto*v=json_tokener_parse_ex(t,raw.c_str(),int(raw.size()));bool ok=json_tokener_get_error(t)==json_tokener_success;json_tokener_free(t);Ptr p(v,json_object_put);check(ok,"strict JSON parse");return p;}
}
}
