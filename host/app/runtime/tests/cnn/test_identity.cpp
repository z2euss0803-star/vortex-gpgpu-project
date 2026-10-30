#include "../../tools/cnn_identity.h"
#include <cassert>
#include <iostream>
#include <string>
static const std::string commit(40,'a');
static const std::string build="H0-LINUX-CNN-I32-V1-20261011T000000Z";
static const std::string boot="e36610f6-8175-4fef-8387-1e7c46b7aa5d";
static std::string valid(){return "{\"scope\":\"CNN_CANDIDATE_IDENTITY_V1\",\"source_commit\":\""+commit+"\",\"build_id\":\""+build+"\",\"contract\":\"cnn-i32-8x8-v1\",\"profile\":\"h0-r4\",\"bit_sha256\":\"70c72d344d0e67fcea13695fdcdd2a54feca76a2889008b99d1db578250739ff\",\"xsa_sha256\":\"01bc96ad997fb0d18967d196649697d2c443e5ad86a27faddd1a59cf278bf2d6\",\"args_bytes\":64,\"programs\":[{\"id\":2,\"bytes\":4096,\"instruction_bytes\":540,\"sha256\":\"1583f8e4d917c6742451b4c6a543f6668c22ae7c39742c8c3cda47a7199f7d71\"},{\"id\":3,\"bytes\":4096,\"instruction_bytes\":164,\"sha256\":\"9680b10581d48f032203884273b35a12fcdd8f67cf3f949dcda399b89f0caad1\"},{\"id\":4,\"bytes\":4096,\"instruction_bytes\":228,\"sha256\":\"a3447a097932e8fc39d093c1560e9f23354dcbd88582ad025835c0e7466097be\"},{\"id\":5,\"bytes\":4096,\"instruction_bytes\":404,\"sha256\":\"f002e3b23c4d1295656658cc11cbc9f449f4113109b81b6b7989a7470106632e\"}]}";}
static bool check(const std::string &s,const std::string &c=commit,const std::string &b=build,const std::string &uuid=boot,const std::string &arch="aarch64",const std::string &kernel="5.4.0-xilinx-v2020.1"){
 std::string reason;return cnn_identity_check(s,c,b,uuid,arch,kernel,&reason);
}
static std::string replace(std::string s,const std::string &a,const std::string &b){auto p=s.find(a);assert(p!=std::string::npos);s.replace(p,a.size(),b);return s;}
int main(){
 auto v=valid();assert(check(v));assert(check(" \n"+v+"\t"));
 assert(!check("{\"notes\":\""+commit+"\"}"));
 assert(!check(replace(v,commit,std::string(40,'b'))));
 assert(!check(replace(v,"cnn-i32-8x8-v1","other")));
 assert(!check(replace(v,"h0-r4","hw4")));
 assert(!check(replace(v,"70c72d","80c72d")));
 assert(!check(replace(v,"01bc96","11bc96")));
 assert(!check(replace(v,"1583f8","2583f8")));
 assert(!check(replace(v,"4096","4095")));
 assert(!check(replace(v,"540","539")));
 assert(!check(replace(v,"\"id\":3","\"id\":2")));
 assert(!check(replace(v,"\"args_bytes\":64","\"args_bytes\":128")));
 assert(!check(v+"trailing"));assert(!check(v.substr(0,v.size()-1)));
 assert(!check(replace(v,"\"source_commit\":", "\"source_commit\":\"bad\",\"source_commit\":")));
 assert(!check(replace(v,"\"source_commit\":", "\"source_commit\":\"bad\",\"source_\\u0063ommit\":")));
 assert(!check(replace(v,"\"args_bytes\":64","\"args_bytes\":\"64\"")));
 assert(!check(replace(v,"\"args_bytes\":64","\"args_bytes\":064")));
 assert(!check(v, "UNSEALED"));assert(!check(v,commit,"H0-LINUX-BACKEND-FIX1-foo"));
 assert(!check(v,commit,build,std::string(36,'z')));assert(!check(v,commit,build,boot,"x86_64"));
 assert(!check(v,commit,build,boot,"aarch64","5.4.0-unknown"));
 assert(!check(std::string(1024*1024+1,' ')));
 std::cout<<"CNN_CANDIDATE_IDENTITY_PASS 25 cases; installed declarations only, not live bit proof\n";
}
