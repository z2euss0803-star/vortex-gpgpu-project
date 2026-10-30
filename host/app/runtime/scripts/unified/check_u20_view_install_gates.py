"""Installer view metadata and pre-worker startup failure behavior, not GPU/DP."""
import pathlib,subprocess,sys,time,json,os
from scripts.unified.check_u11 import sha,artifact
def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 receipt=dict(scope='HOST_VIEW_INSTALL_METADATA_AND_STARTUP_FAILURE_NOT_GPU_DP',commands=[],gpu_raw_cycles=None)
 def run(argv):
  argv=list(map(str,argv));log=out/('command-%02d.log'%len(receipt['commands']));start=time.monotonic()
  with log.open('xb') as f:r=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=None if argv[0]=='make' else 60)
  text=log.read_text(errors='replace');receipt['commands'].append(dict(argv=argv,natural_exit=r.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));print(text[-1600:],flush=True);return r.returncode,text
 try:
  code,_=run([sys.executable,'tests/v2/catalog_fixture.py',out/'catalog.bin']);assert code==0
  jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  code,_=run(['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1',out/'test_installed_media',out/'test_daemon','-j'+str(jobs)]);assert code==0,'compile is not behavior RED'
  code,text=run([out/'test_installed_media'])
  if mode=='red':assert code==1 and 'U20 sealed HUNet source/labels view must be installable' in text
  else:assert code==0
  code,text=run([out/'test_daemon',out/'catalog.bin',out/'journal','startup-journal'])
  if mode=='red':assert code==1 and 'U20 failed startup must retire shared display/results/port' in text
  else:assert code==0 and 'U20_STARTUP_JOURNAL_FIRST_ERROR_ORDERED_RETIREMENT_NO_CYCLE_NO_GPU_PASS_HOST' in text
  receipt.update(natural_exit=0,functional_red=mode=='red')
 except BaseException as e:receipt.update(natural_exit=1,error=repr(e));raise
 finally:
  receipt['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
if __name__=='__main__':main()
