"""Actual daemon lifecycle and legacy regression; no device or GPU execution."""
import json,os,pathlib,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha

def main():
 name,mode=sys.argv[1:];assert mode in ('red','red-drain','async-red','cancel-red','safety-red','normal','sanitize','thread')
 jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='U20_NATIVE_DAEMON_LIFECYCLE_HOST_CONTROL_NOT_GPU',gpu_raw_cycles=None,mode=mode,commands=[],artifacts=[])
 def run(argv,expected=0):
  argv=list(map(str,argv));start=time.monotonic();log=out/('command-%03d.log'%len(rec['commands']));limit=None if argv[0]=='make' else 240
  with log.open('xb') as stream:
   try:p=subprocess.run(argv,stdout=stream,stderr=subprocess.STDOUT,timeout=limit)
   except subprocess.TimeoutExpired:
    rec['commands'].append(dict(argv=argv,natural_exit=None,elapsed_s=time.monotonic()-start,controller_timeout_s=limit,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');raise
  text=log.read_text(errors='replace');rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-start,controller_timeout_s=limit,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');print(text[-2300:],flush=True);assert p.returncode==expected,'natural exit '+str(p.returncode);assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'));return text
 try:
  command=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):command+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  run(command+[out/'test_daemon',out/'catalog-fixture.bin','-j'+str(jobs)])
  cases=('lifecycle',) if mode=='red' else ('shutdown-unreaped',) if mode=='red-drain' else ('slow-preparation',) if mode in ('async-red','cancel-red') else ('close-preparation','identity-preparation') if mode=='safety-red' else ('lifecycle','lifecycle-activate','lifecycle-quiesce','lifecycle-display','lifecycle-resources','shutdown-unreaped','shutdown-held','slow-preparation','close-preparation','identity-preparation','error-preparation','throw-preparation','deferred','legacy','fault')
  for case in cases:
   text=run([out/'test_daemon',out/'catalog-fixture.bin',out/('journal-'+case),case],1 if mode in ('red','red-drain','async-red','cancel-red','safety-red') else 0)
   if mode=='red':assert 'U20 native lifecycle activate must run exactly once' in text;rec['functional_red']='ACTIVATE_AND_ORDERED_NATIVE_INGRESS_DISPLAY_PROGRAM_LIFECYCLE_MISSING'
   if mode=='red-drain':assert 'U20 shutdown must retire completed native backing' in text;rec['functional_red']='COMPLETED_NATIVE_BACKING_NOT_DRAINED_WITHOUT_CLIENT_REAP'
   if mode=='async-red':assert 'U20 native RPC admission must not wait for model preparation' in text;rec['functional_red']='RPC_CONTROL_THREAD_BLOCKED_INSIDE_NATIVE_FACTORY'
   if mode=='cancel-red':assert 'U20 active preparation cancellation must remain CANCEL_PENDING until worker finishes' in text;rec['functional_red']='ACTIVE_PREPARATION_REPORTED_CANCELLED_BEFORE_WORKER_FINISHES'
   if mode=='safety-red':
    assert ('U20 closing during preparation must never start a GPU task after quiesce fails' if case=='close-preparation' else 'U20 preparation exception must finalize metadata error and release idle reservation') in text;rec.setdefault('functional_reds',[]).append(case)
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
