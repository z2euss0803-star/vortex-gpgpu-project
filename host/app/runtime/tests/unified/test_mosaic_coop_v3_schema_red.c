#include <vxl/v2/program.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){
 unsigned char b[4096];struct vxl_v2_schema_header h;struct vxl_v2_schema_field f;unsigned abi=0,options=0;FILE *p;size_t n;
 if(argc!=2||(p=fopen(argv[1],"rb"))==NULL){return 74;}n=fread(b,1,sizeof b,p);if(ferror(p)||!feof(p)||fclose(p)||vxl_v2_schema_check(b,(vxl_u32)n,128))return 75;
 memcpy(&h,b,64);for(unsigned i=0;i<h.field_count;i++){memcpy(&f,b+64+i*64,64);if(f.kind==VXL_V2_SCALAR32&&f.arg_offset==0)abi=f.min_value==3&&f.max_value==3;if(f.kind==VXL_V2_SCALAR32&&f.arg_offset==124)options=f.min_value==0&&f.max_value==1;}
 if(!abi||!options){fprintf(stderr,"EXPECTED_RED_SCHEMA_NEEDS_EXACT_ABI3_AND_OPTIONS_BIT0 abi=%u options=%u\n",abi,options);return 76;}return 0;
}

