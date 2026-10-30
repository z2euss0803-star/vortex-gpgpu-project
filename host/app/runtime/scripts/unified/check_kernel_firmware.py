"""Run the real common C boot binder on production files, with HOST MMIO only."""
import pathlib,json,subprocess,sys,time
from scripts.unified.bind_kernel import firmware_inputs
from scripts.unified.cross_common import new_build_output,sha

def main():
 executable=pathlib.Path(sys.argv[1]).resolve();out=new_build_output(pathlib.Path.cwd(),sys.argv[2]);record=dict(scope='PRODUCTION_FIRMWARE_COMMON_C_BOOT_BIND_HOST_REGISTERS_NOT_GPU',commands=[],providers=[],gpu_raw_cycles=None)
 try:
  for flag in (1,2):
   bundle=firmware_inputs('.',flag);folder=out/('s8' if flag==1 else 'p8');folder.mkdir();catalog=folder/'catalog-v1.bin';execution=folder/'execution-v1.bin';catalog.write_bytes(bundle['catalog']);execution.write_bytes(bundle['execution'])
   args=[str(executable),str(catalog),str(execution),str(flag)];log=folder/'host-bind.log';start=time.monotonic()
   with log.open('xb') as stream:p=subprocess.run(args,stdout=stream,stderr=subprocess.STDOUT)
   text=log.read_text(errors='replace');record['commands'].append(dict(argv=args,natural_exit=p.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));print(text,flush=True)
   if p.returncode or 'U23_PRODUCTION_FIRMWARE_SHARED_C_BIND_PASS_HOST_REGISTERS_ONLY' not in text or any(x in text for x in ('ERROR: AddressSanitizer','runtime error:','WARNING: ThreadSanitizer')):raise ValueError('actual C production-file binding failed; first raw log retained')
   record['providers'].append(dict(provider_flags=flag,catalog_sha256=sha(catalog),execution_sha256=sha(execution),entries=len(bundle['entries']),watchdog=bundle['watchdog'],target_executed=False,gpu_raw_cycles=None))
  record['natural_exit']=0
 except BaseException as error:record.update(natural_exit=1,error=repr(error));raise
 finally:(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')

if __name__=='__main__':main()
