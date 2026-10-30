"""Sealed provider-isolated installation metadata, never native GPU proof."""
import pathlib,subprocess,json,sys,time,os
from scripts.unified.check_u11 import sha,artifact
def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='SEALED_MEDIA_INSTALLATION_METADATA_ONLY_NOT_NATIVE_EXECUTION_GPU_OR_DP',gpu_raw_cycles=None,commands=[],artifacts=[])
 def run(args):
  args=list(map(str,args));log=out/('command-%02d.log'%len(rec['commands']));begin=time.monotonic()
  with log.open('xb') as f:r=subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=None if args[0]=='make' else 30)
  rec['commands'].append(dict(argv=args,natural_exit=r.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=log.read_text(errors='replace');print(text[-1800:],flush=True);return r.returncode,text
 try:
  jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  code,_=run(['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1',out/'test_installed_media','-j'+str(jobs)]);assert code==0
  code,text=run([out/'test_installed_media'])
  if mode=='red':assert code==1 and 'U20 sealed S8 IMAGE installation must be admitted without inventing NN or P8 support' in text;rec['functional_red']='SEALED_IMAGE_AND_P8_INSTALLATION_NOT_SUPPORTED_YET'
  else:assert code==0 and 'U20_INSTALLATION_V2_S8_NN_IMAGE_P8_ISOLATED_TYPED_BINDINGS_AND_V1_COMPAT_PASS_METADATA_ONLY' in text
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  rec['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
