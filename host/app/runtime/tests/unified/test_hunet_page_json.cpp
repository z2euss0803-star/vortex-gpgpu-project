#include "unified/service/hunet_page_json.hpp"
#include <cstdio>
#include <cstdlib>
using namespace vxunified::hunet_page_detail;
int main(){unsigned passed=0;auto accept=[&](const std::string&s,bool want){bool got=false;try{auto o=parse_index(s);got=bool(o);}catch(const std::exception&){}if(got!=want){std::fprintf(stderr,"PAGE520_JSON_ASSERT want=%d input=%s\n",int(want),s.c_str());std::exit(1);}++passed;};
 for(const auto*s:{"{\"x\":1.5}","{\"x\":-0.0}","{\"x\":1e-20}","{\"x\":[12.5,10,9,8,7],\"s\":\"escaped \\\" 1e999\",\"null\":null}","{\"x\":18446744073709551615}"}){accept(s,true);}
 for(const auto*s:{"{\"x\":1e999}","{\"x\":-1e999}","{\"x\":1.0,\"x\":2.0}","{\"x\":1.0,\"\\u0078\":2.0}","{\"x\":18446744073709551616}","{\"x\":-9223372036854775809}","{\"x\":NaN}","{\"x\":Infinity}","{\"x\":01.0}","{\"x\":1.}","{\"x\":.1}","{\"x\":1e}","{\"x\":1e+}","{\"x\":1.2,}","{\"x\":1.0} []","{\"x\":\"\\u0000\"}"}){accept(s,false);}
 accept(std::string("{\"x\":\"")+char(0xff)+"\"}",false);accept(std::string(34,'[')+"1.0"+std::string(34,']'),false);accept(std::string(256u<<10,' ')+"{}",false);
 auto o=parse_index("{\"x\":-0.0}");float f=float(json_object_get_double(vxnn::json::member(o.get(),"x")));if(!std::signbit(f)){return 2;}std::printf("PAGE520_JSON_PASS assertions=%u signed_zero_preserved=1 strict_float_ONLY\n",passed+1);
}
