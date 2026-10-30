#include "cnn_identity.h"
#include <map>
#include <vector>
#include <stdexcept>
#include <cctype>
#include <regex>
namespace {
struct Json {
 enum Kind {OBJECT,ARRAY,STRING,NUMBER,LITERAL} kind;
 std::string text;std::map<std::string,Json> object;std::vector<Json> array;
 explicit Json(Kind k):kind(k){}
 const Json &at(const std::string &k)const {if(kind!=OBJECT)throw std::runtime_error("object required");return object.at(k);}
};
class Parser {
 const std::string &s;size_t p=0;
 [[noreturn]] void bad(){throw std::runtime_error("malformed or duplicate JSON");}
 void ws(){while(p<s.size()&&(s[p]==' '||s[p]=='\t'||s[p]=='\n'||s[p]=='\r'))++p;}
 bool take(char c){ws();if(p<s.size()&&s[p]==c){++p;return true;}return false;}
 unsigned hex4(){unsigned v=0;for(int i=0;i<4;++i){if(p==s.size())bad();char c=s[p++];v<<=4;if(c>='0'&&c<='9')v+=c-'0';else if(c>='a'&&c<='f')v+=c-'a'+10;else if(c>='A'&&c<='F')v+=c-'A'+10;else bad();}return v;}
 static void utf8(std::string &o,unsigned c){if(c<128)o+=char(c);else if(c<2048){o+=char(0xc0|(c>>6));o+=char(0x80|(c&63));}else if(c<65536){o+=char(0xe0|(c>>12));o+=char(0x80|((c>>6)&63));o+=char(0x80|(c&63));}else{o+=char(0xf0|(c>>18));o+=char(0x80|((c>>12)&63));o+=char(0x80|((c>>6)&63));o+=char(0x80|(c&63));}}
 std::string str(){if(!take('"'))bad();std::string o;while(p<s.size()){unsigned char c=s[p++];if(c=='"')return o;if(c<32)bad();if(c!='\\'){o+=char(c);continue;}if(p==s.size())bad();char e=s[p++];switch(e){case '"':case '\\':case '/':o+=e;break;case 'b':o+='\b';break;case 'f':o+='\f';break;case 'n':o+='\n';break;case 'r':o+='\r';break;case 't':o+='\t';break;case 'u':{unsigned u=hex4();if(u>=0xd800&&u<=0xdbff){if(p+2>s.size()||s[p++]!='\\'||s[p++]!='u')bad();unsigned lo=hex4();if(lo<0xdc00||lo>0xdfff)bad();u=0x10000+((u-0xd800)<<10)+(lo-0xdc00);}else if(u>=0xdc00&&u<=0xdfff)bad();utf8(o,u);break;}default:bad();}}bad();}
 Json value(unsigned depth){ws();if(depth>64||p==s.size())bad();if(s[p]=='{'){++p;Json v(Json::OBJECT);if(take('}'))return v;do{auto k=str();if(!take(':'))bad();if(!v.object.emplace(k,value(depth+1)).second)bad();if(take('}'))return v;}while(take(','));bad();}
 if(s[p]=='['){++p;Json v(Json::ARRAY);if(take(']'))return v;do{v.array.push_back(value(depth+1));if(take(']'))return v;}while(take(','));bad();}
 if(s[p]=='"'){Json v(Json::STRING);v.text=str();return v;}
 size_t begin=p;
 if(s[p]=='-'||(s[p]>='0'&&s[p]<='9')){if(s[p]=='-')++p;if(p==s.size())bad();if(s[p]=='0')++p;else{if(s[p]<'1'||s[p]>'9')bad();while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;}if(p<s.size()&&s[p]=='.'){++p;size_t start=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;if(p==start)bad();}if(p<s.size()&&(s[p]=='e'||s[p]=='E')){++p;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;size_t start=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;if(p==start)bad();}Json v(Json::NUMBER);v.text=s.substr(begin,p-begin);return v;}
 for(auto lit:{"true","false","null"}){std::string l(lit);if(s.compare(p,l.size(),l)==0){p+=l.size();Json v(Json::LITERAL);v.text=l;return v;}}bad();}
public:explicit Parser(const std::string &input):s(input){}Json parse(){auto v=value(0);ws();if(p!=s.size())bad();return v;}
};
void equal(const Json &v,const std::string &k,Json::Kind kind,const std::string &expected){const auto &a=v.at(k);if(a.kind!=kind||a.text!=expected)throw std::runtime_error("identity mismatch: "+k);}
}
bool cnn_identity_check(const std::string &manifest,const std::string &commit,const std::string &build,const std::string &boot,const std::string &machine,const std::string &kernel,std::string *reason){
 try{
  if(manifest.empty()||manifest.size()>1024*1024)throw std::runtime_error("manifest size");
  if(!std::regex_match(commit,std::regex("[0-9a-f]{40}"))||!std::regex_match(build,std::regex("H0-LINUX-CNN-I32-V1-[0-9]{8}T[0-9]{6}Z"))||!std::regex_match(boot,std::regex("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}"))||machine!="aarch64"||kernel!="5.4.0-xilinx-v2020.1")throw std::runtime_error("boot/tool/kernel identity");
  auto m=Parser(manifest).parse();
  for(auto kv:{std::pair<const char*,std::string>{"scope","CNN_CANDIDATE_IDENTITY_V1"},{"source_commit",commit},{"build_id",build},{"contract","cnn-i32-8x8-v1"},{"profile","h0-r4"},{"bit_sha256","70c72d344d0e67fcea13695fdcdd2a54feca76a2889008b99d1db578250739ff"},{"xsa_sha256","01bc96ad997fb0d18967d196649697d2c443e5ad86a27faddd1a59cf278bf2d6"}})equal(m,kv.first,Json::STRING,kv.second);
  equal(m,"args_bytes",Json::NUMBER,"64");
  const auto &programs=m.at("programs");if(programs.kind!=Json::ARRAY||programs.array.size()!=4)throw std::runtime_error("program inventory");
  const char *digests[]={"1583f8e4d917c6742451b4c6a543f6668c22ae7c39742c8c3cda47a7199f7d71","9680b10581d48f032203884273b35a12fcdd8f67cf3f949dcda399b89f0caad1","a3447a097932e8fc39d093c1560e9f23354dcbd88582ad025835c0e7466097be","f002e3b23c4d1295656658cc11cbc9f449f4113109b81b6b7989a7470106632e"};
  const int instructions[]={540,164,228,404};
  for(size_t i=0;i<4;++i){auto &p=programs.array[i];equal(p,"id",Json::NUMBER,std::to_string(i+2));equal(p,"bytes",Json::NUMBER,"4096");equal(p,"instruction_bytes",Json::NUMBER,std::to_string(instructions[i]));equal(p,"sha256",Json::STRING,digests[i]);}
  if(reason)reason->clear();
  return true;
 }catch(const std::exception &e){if(reason)*reason=e.what();return false;}
}
