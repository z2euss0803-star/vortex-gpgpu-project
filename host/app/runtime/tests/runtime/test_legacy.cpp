#include <vortex.h>
#include <cassert>
#include <fstream>
#include <cstdlib>
#include <string>
int main(){const char *path=getenv("VXL_LEGACY_TRACE");assert(path);std::ofstream(path).close();vx_device_h d=nullptr;assert(!vx_dev_open(&d));assert(!vx_dev_close(d));
 std::ifstream input(path);std::string line;unsigned open=0,close=0,dcr=0,mpm=0;
 while(std::getline(input,line)){open+=line=="OPEN";close+=line=="CLOSE";dcr+=line=="DCR";mpm+=line=="MPM";}
 assert(open==1&&close==1&&dcr==5&&mpm>=2);return 0;}
