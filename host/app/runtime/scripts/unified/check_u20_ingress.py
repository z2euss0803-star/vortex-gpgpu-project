"""Native receiver actual full720 wire/control and common preparation8 integration."""
import pathlib,subprocess,sys,json,time,os
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal','sanitize','thread')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='HOST_NATIVE_IMAGE_INGRESS_PROTOCOL_NOT_GPU_OR_DP',gpu_raw_cycles=None,commands=[],artifacts=[])
 def run(argv,expected=0):
  argv=list(map(str,argv));log=out/('command-%03d.log'%len(rec['commands']));begin=time.monotonic()
  with log.open('xb') as f:r=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT)
  rec['commands'].append(dict(argv=argv,natural_exit=r.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));text=log.read_text(errors='replace');print(text[-2000:],flush=True);assert r.returncode==expected;assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'));return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  elf=out/'test_native_image_ingress';run(cmd+[elf,'-j'+str(jobs)])
  for case in ('declared','ingress'):
   text=run([elf,'data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin',out/case,case],1 if mode=='red' else 0)
   if mode=='red':assert 'U20 native receiver must bind common roots and account persistent ingress without forged measurement' in text
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  rec['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
