"""Shared native preparation admission, not a private per-app queue."""
import pathlib,subprocess,json,sys,os,time
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal','sanitize','thread');out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='U20_COMMON_PREPARATION_QUEUE_HOST_CONTROL_NOT_GPU',mode=mode,gpu_raw_cycles=None,commands=[])
 def run(argv,expected=0):
  argv=list(map(str,argv));start=time.monotonic();log=out/('command-%02d.log'%len(rec['commands']))
  with log.open('xb') as f:p=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT)
  text=log.read_text(errors='replace');rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));print(text[-1400:],flush=True);assert p.returncode==expected;return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  run(cmd+[out/'test_preparation','-j'+os.environ.get('VX_UNIFIED_JOBS','1')]);text=run([out/'test_preparation'],1 if mode=='red' else 0)
  if mode=='red':assert 'U20 native preparation must reserve the same global queue8' in text;rec['functional_red']='GLOBAL_NATIVE_PREPARATION_RESERVATION_MISSING'
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  rec['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
