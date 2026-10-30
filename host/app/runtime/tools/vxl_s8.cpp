#include "s8/session.h"
#include <cstdio>
int main(int argc,char **argv){
 if(argc==2&&std::string(argv[1])=="--help"){puts("vxl_s8 --manifest RELEASE --catalog CASES --case ID --run-id HEX32 --evidence /run/NEW_DIR [--core-mask 1|2|4|8|15] [--owner-warp 0..3] [--phase 0|1]\nOne explicit case only; default phase0. No automatic retries, board resets or SD operations.");return 0;}
 S8Options options;int rc=s8_parse_options(argc,argv,&options);if(rc){fprintf(stderr,"explicit manifest/catalog/case/run-id/evidence required: %d\n",rc);return 2;}
 rc=s8_session_run_selected(options);if(rc){fprintf(stderr,"S8 FAIL_STOP error=%d; retain evidence directory\n",rc);return 1;}puts("S8_CASE_CAPTURED; independent audit required before board acceptance");return 0;
}
