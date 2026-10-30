"""Native P8 receiver shares common budget, queue and cookie namespace."""
import pathlib,subprocess,sys,json,time,os
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal','sanitize','thread')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='HOST_NATIVE_TEXTURE_ADMISSION_QUEUE_BUDGET_NOT_GPU_DP',gpu_raw_cycles=None,commands=[],artifacts=[])
 def run(argv,expected=0):
  argv=list(map(str,argv));log=out/('command-%03d.log'%len(rec['commands']));start=time.monotonic()
  with log.open('xb') as f:r=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=None if argv[:3]==['make','-f','Makefile.unified'] else 180)
  rec['commands'].append(dict(argv=argv,natural_exit=r.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=log.read_text(errors='replace');print(text[-1800:],flush=True);assert r.returncode==expected;assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'));return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  elf=out/'test_native_p8_texture_ingress';run(cmd+[elf,'-j'+str(jobs)])
  for case in ('declared','texture-ingress','wire-ingress','wire-cancel','wire-abandon'):
   expected=1 if mode=='red' or (case=='wire-ingress' and os.environ.get('VX_EXPECT_NATIVE_WIRE_RED')=='1') else 0
   text=run([elf,'data/p8/programs/catalog.bin','data/p8/programs','unused',out/case,case],expected)
   if mode=='red':assert 'U20 native P8 texture receiver must share common roots budget queue and cookie allocator' in text
   if case=='wire-ingress' and expected and mode!='red':assert 'U20 native P8 wire ingress must share bounded original receiver path' in text
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  rec['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
