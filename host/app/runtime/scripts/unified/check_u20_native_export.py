"""File-only retained result bridge; no GPU/DP or new model accuracy."""
import pathlib,subprocess,sys,time,json,os
from scripts.unified.check_u11 import sha,artifact
from scripts.unified.capture_native_private import capture
def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal','bridge-red','bridge');out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);record=dict(scope='NATIVE_RETAINED_FILE_EXPORT_HOST_NOT_INFERENCE_GPU_DP',commands=[],gpu_raw_cycles=None)
 def run(argv):
  argv=list(map(str,argv));log=out/('command-%02d.log'%len(record['commands']));begin=time.monotonic()
  with log.open('xb') as f:r=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=None if argv[0]=='make' else 60)
  text=log.read_text(errors='replace');record['commands'].append(dict(argv=argv,natural_exit=r.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));record.setdefault('private_evidence',[]).extend(capture(text,out,len(record['commands'])));print(text[-1800:],flush=True);return r.returncode,text
 try:
  jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2);code,_=run(['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1',out/'test_native_result_export','-j'+str(jobs)]);assert code==0,'compile is not behavior RED'
  code,text=run([out/'test_native_result_export'])
  if mode=='red':assert code==1 and 'U20 native Mosaic result needs durable file export with exact lineage' in text;record['functional_red']='NATIVE_RETAINED_MOSAIC_EXPORT_NOT_BOUND'
  else:
   assert code==0 and 'U20_NATIVE_MOSAIC_FILE_EXPORT_EXACT_BYTES_LINEAGE_IDEMPOTENCY_SHARED_OWNER_PASS_HOST_NOT_INFERENCE_GPU_DP' in text
   if mode.startswith('bridge'):
    source=next(x.split('=',1)[1] for x in text.splitlines() if x.startswith('NATIVE_EXPORTED_MANIFEST='))
    from scripts.unified.texture_package import build_package
    package=out/'native-texture';meta=build_package(source,package);record['texture_package_id']=meta['package_id']
    code,_=run(['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1',out/'test_native_export_bridge','-j'+str(jobs)]);assert code==0,'compile is not behavior RED'
    code,text=run([out/'test_native_export_bridge',source,package,meta['package_id']])
    if mode=='bridge-red':assert code==1 and 'native HOST scope must survive' in text;record['bridge_red']='NATIVE_ORIGIN_NOT_VALIDATED_OR_PRESERVED'
    else:assert code==0 and 'U20_NATIVE_MOSAIC_FILE_TO_ORIGINAL_P8_TEXTURE_SCOPE_LINEAGE_AND_REJECTIONS_PASS_HOST_NOT_GPU_DP' in text
    code,text=run(['python3','-m','unittest','tests.unified.test_texture_package.Package.test_native_origin_mismatch_is_not_ignored'])
    if mode=='bridge-red':assert code==1 and 'not raised' in text;record['metadata_red']='FORGED_NATIVE_ORIGIN_IGNORED'
    else:assert code==0
  record['natural_exit']=0
 except BaseException as e:record.update(natural_exit=1,error=repr(e));raise
 finally:
  record['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()
