"""Serial checkpoint regression, not full U06 or application acceptance."""
import json,subprocess,sys

def main():
 commands=[
  [sys.executable,'scripts/unified/check_u05.py'],
  ['make','-f','Makefile.unified','test-v2-provider','test-v2-control','test-unified-backend','-j1'],
  ['make','-f','Makefile.unified','OUT=build/U06-san-015','SANITIZE=address,undefined','test-v2-provider','test-v2-control','test-unified-backend','-j1'],
  ['make','-f','Makefile.unified','OUT=build/unified-tsan','SANITIZE=thread','test-v2-dispatch','test-unified-backend','test-unified-resources','-j1']]
 for i,command in enumerate(commands):
  print(json.dumps(dict(stage=i,command=command,scope='U06_COMPONENT_REGRESSION_NOT_BOARD',gpu_raw_cycles=None)),flush=True)
  rc=subprocess.call(command)
  print(json.dumps(dict(stage=i,natural_exit=rc,gpu_raw_cycles=None)),flush=True)
  if rc:return rc
 return 0

if __name__=='__main__':sys.exit(main())
