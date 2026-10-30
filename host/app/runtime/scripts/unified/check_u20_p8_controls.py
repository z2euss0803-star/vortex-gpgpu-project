"""Public B controls, existing P8 startup/visual oracle, no board execution."""
import pathlib,subprocess,sys,time,json,os
from scripts.unified.check_u11 import sha,artifact
from scripts.unified.capture_native_private import capture
def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal','detail-red','file-red','file','file-permission','all','sanitize','thread');out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);record=dict(scope='P8_PUBLIC_CONTROL_HOST_C_CPU_ORACLE_NOT_GPU_DP',commands=[],gpu_raw_cycles=None);complete=mode in ('all','sanitize','thread')
 def run(argv):
  argv=list(map(str,argv));p=out/('command-%02d.log'%len(record['commands']));start=time.monotonic()
  with p.open('xb') as f:r=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=None if argv[0]=='make' else 600)
  text=p.read_text(errors='replace');record['commands'].append(dict(argv=argv,natural_exit=r.returncode,elapsed_s=time.monotonic()-start,log=str(p),sha256=sha(p)));record.setdefault('private_evidence',[]).extend(capture(text,out,len(record['commands'])));print(text[-2200:],flush=True);assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:','INSTALLATION_ROLLBACK_','NATIVE_FILES_SEAL_ERROR'));return r.returncode,text
 try:
  code,_=run([sys.executable,'-m','unittest','tests.unified.test_native_origin']);assert code==0
  jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  build=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):build+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  recipe=list(build)
  build+=[out/'test_installed_p8',out/'vxgpud']
  if complete:build+=[out/'test_daemon',out/'catalog-fixture.bin']
  code,_=run(build+['-j'+str(jobs)]);assert code==0,'compile is not behavior RED'
  args=[out/'test_installed_p8','data/p8/programs/catalog.bin','data/p8/programs','data/p8',out/'p8-output','file-red' if mode=='file-red' else 'controls']
  if mode in ('file','file-permission') or complete:
   from scripts.unified.texture_pillow import dependency
   record['pillow']=dependency()
   from tests.unified.test_texture_package import Package
   from scripts.unified.texture_package import build_package
   base=out/'file-input';base.mkdir();Package().fixture(base/'source');metadata=build_package(base/'source/result.json',base/'package');record['texture_package_id']=metadata['package_id']
   args[-1]=mode if not complete else 'file';args.extend([base/'source/result.json',base/'package',metadata['package_id']])
  code,text=run(args)
  if mode=='red':assert code==1 and 'U20 installed P8 must register B controls in the common request service' in text;record['functional_red']='P8_B_CONTROL_ENTRY_NOT_INSTALLED'
  elif mode=='detail-red':assert code==1 and 'P8 control snapshot must retain display pose and gallery deadline' in text;record['functional_red']='P8_CONTROL_SNAPSHOT_INCOMPLETE'
  elif mode=='file-red':assert code==1 and 'U20 installed P8 must expose sealed Mosaic texture file admission in common service' in text;record['functional_red']='P8_SEALED_MOSAIC_TEXTURE_FILE_ENTRY_MISSING'
  elif mode=='file-permission':assert code==0 and 'U20_INSTALLED_P8_PERMISSION_ERROR_QUERYABLE_NEW_ADMISSION_CLOSED_NO_TEXTURE_GPU_PASS_HOST' in text
  else:
   assert code==0 and 'U20_INSTALLED_P8_PUBLIC_COMMON_QUEUE_CPU_CONTROL_AND_ORIGINAL_FRAME_RETIREMENT_PASS_HOST_ORACLE_NOT_GPU_DP' in text
   if mode=='file' or complete:assert 'U20_INSTALLED_P8_PUBLIC_MOSAIC_FILE_SHARED_QUEUE_LINEAGE_ORIGINAL_RENDER_AND_RETIREMENT_PASS_HOST_NOT_GPU_DP' in text
  if complete:
   args[5]='file-permission';code,text=run(args);assert code==0 and 'U20_INSTALLED_P8_PERMISSION_ERROR_QUERYABLE_NEW_ADMISSION_CLOSED_NO_TEXTURE_GPU_PASS_HOST' in text
   for case in ('lifecycle','lifecycle-activate','lifecycle-quiesce','lifecycle-display','lifecycle-resources','shutdown-unreaped','shutdown-held','slow-preparation','close-preparation','identity-preparation','error-preparation','throw-preparation','deferred','legacy','fault','startup-journal'):
    code,_=run([out/'test_daemon',out/'catalog-fixture.bin',out/('journal-'+case),case]);assert code==0
   code,_=run(recipe+['test-unified-daemon-cli','-j1']);assert code==0
  record['natural_exit']=0
 except BaseException as e:record.update(natural_exit=1,error=repr(e));raise
 finally:
  record['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()
