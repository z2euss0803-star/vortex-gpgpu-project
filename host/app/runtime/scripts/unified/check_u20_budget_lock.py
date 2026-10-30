"""Actual Runtime callback lock regression, no device execution."""
import pathlib,subprocess,sys,time,json,os,signal
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='HOST_METADATA_RUNTIME_LOCK_ORDER_ZERO_GPU',gpu_raw_cycles=None,commands=[],artifacts=[])
 def run(args):
  args=list(map(str,args));p=out/('command-%02d.log'%len(rec['commands']));begin=time.monotonic()
  with p.open('xb') as f:r=subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=None if args[0]=='make' else 30)
  rec['commands'].append(dict(argv=args,natural_exit=r.returncode,elapsed_s=time.monotonic()-begin,log=str(p),sha256=sha(p)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');print(p.read_text(errors='replace')[-1500:],flush=True);return r.returncode,p.read_text(errors='replace')
 try:
  jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  code,_=run(['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1',out/'test_host_memory','-j'+str(jobs)]);assert code==0
  code,text=run([out/'test_host_memory','metadata-budget']);assert 'U20_METADATA_BUDGET_CALLBACK_BEGIN' in text
  if mode=='red':assert code==-signal.SIGALRM and 'U20_METADATA_BUDGET_CALLBACK_NO_REENTRY_DEADLOCK_PASS' not in text;rec['functional_red']='OWN_HOST_ALARM_AFTER_EXACT_METADATA_BUDGET_CALLBACK_DEADLOCK_NO_DEVICE_EXECUTION'
  else:assert code==0 and 'U20_METADATA_BUDGET_CALLBACK_NO_REENTRY_DEADLOCK_PASS' in text
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  rec['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
