"""Full U01-U06 mechanism regression; no device/PL/SD and no GPU measurement."""
import json,pathlib,subprocess,sys
def main():
 name=sys.argv[1]
 assert name.replace('-','').isalnum()
 commands=[[sys.executable,'scripts/unified/check_u05.py']]
 targets=['test-v2-provider','test-v2-control','test-unified-backend','test-v2-execution']
 commands+=[['make','-f','Makefile.unified','OUT=build/'+name+'-host']+targets+['-j1'],['make','-f','Makefile.unified','OUT=build/'+name+'-san','SANITIZE=address,undefined']+targets+['-j1'],['make','-f','Makefile.unified','OUT=build/'+name+'-tsan','SANITIZE=thread','test-v2-dispatch','test-unified-backend','test-unified-resources','-j1']]
 for stage,command in enumerate(commands):
  print(json.dumps(dict(stage=stage,command=command,scope='U01_U06_SOFTWARE_MECHANISM_NOT_RELEASE_NOT_BOARD',gpu_raw_cycles=None)),flush=True)
  rc=subprocess.call(command)
  print(json.dumps(dict(stage=stage,natural_exit=rc,gpu_raw_cycles=None)),flush=True)
  if rc:return rc
 return 0
if __name__=='__main__':sys.exit(main())
