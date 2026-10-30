"""Force actual Result<void> return-copy allocation failures; no GPU work."""
import pathlib,subprocess,sys,os,json,time
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal');out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='HOST_PORT_RETURN_COPY_ALLOCATION_NOT_GPU_OR_BOARD',gpu_raw_cycles=None,commands=[],artifacts=[])
 try:
  commands=[(['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1',str(out/'test_port_result_alloc'),str(out/'catalog-fixture.bin'),'-j'+os.environ.get('VX_UNIFIED_JOBS','1')],0),([str(out/'test_port_result_alloc'),str(out/'catalog-fixture.bin')],1 if mode=='red' else 0)]
  for argv,expected in commands:
   log=out/('command-%02d.log'%len(rec['commands']));begin=time.monotonic()
   with log.open('xb') as f:p=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=None if argv[0]=='make' else 30)
   text=log.read_text(errors='replace');print(text[-2200:],flush=True);rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,log=str(log),sha256=sha(log),elapsed_s=time.monotonic()-begin));assert p.returncode==expected
  if mode=='red':assert 'U20_PORT_RESULT_COPY_EXCEPTION_ASSERT' in text and 'Resource deadlock avoided' in text
  else:assert 'ORIGINAL_EXCEPTION_AND_RESERVATION_PASS_HOST_ONLY' in text
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
