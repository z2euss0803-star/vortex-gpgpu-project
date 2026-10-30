"""One serial, fail-fast U05 software regression; raw stdout retained by runner."""
import json,pathlib,subprocess,sys

def main():
 root=pathlib.Path.cwd()
 targets=['test-unified-freeze','test-unified-wire','test-unified-identity','test-v2-objects','test-unified-resources','test-v2-buffer-adapter','test-unified-catalog','test-unified-catalog-builder','test-v2-program','test-v2-relocate','test-v2-catalog-driver','test-v2-private-init','test-v2-jobs','test-v2-batch','test-v2-close','test-v2-poll','test-v2-dispatch']
 commands=[['make','-f','Makefile.unified','OUT=build/unified-host']+targets+['-j1'],
  ['make','-f','Makefile.unified','OUT=build/unified-san','SANITIZE=address,undefined']+targets+['-j1'],
  ['make','CFLAGS=-std=c11 -O2 -g -Wall -Wextra -Werror -Iinclude -I. -fPIC -fdebug-prefix-map='+str(root)+'=/usr/src/vxl','CXXFLAGS=-std=c++17 -O2 -g -Wall -Wextra -Werror -Iinclude -I. -fPIC -fdebug-prefix-map='+str(root)+'=/usr/src/vxl','test-contract','test-uapi','test-memory','test-jobs','test-probe','test-copy','test-submit','test-runtime','-j1']]
 for i,command in enumerate(commands):
  print(json.dumps(dict(stage=i,command=command,scope='HOST_SOFTWARE_NOT_BOARD',gpu_raw_cycles=None)),flush=True)
  code=subprocess.call(command)
  print(json.dumps(dict(stage=i,natural_exit=code,gpu_raw_cycles=None)),flush=True)
  if code:return code
 return 0

if __name__=='__main__':sys.exit(main())
