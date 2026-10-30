"""Actual installed NN service -> immutable export -> frozen P8 file pipeline."""
import pathlib,subprocess,sys,time,json,os,struct
from scripts.unified.mosaic_program import selected_directory
from scripts.unified.check_u11 import sha,artifact
from scripts.unified.capture_native_private import capture

def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal');out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);record=dict(scope='INSTALLED_MOSAIC_EXPORT_TO_P8_TEXTURE_HOST_PROTOCOL_NOT_NN_MATH_GPU_DP',commands=[],gpu_raw_cycles=None)
 def run(argv):
  argv=list(map(str,argv));log=out/('command-%02d.log'%len(record['commands']));start=time.monotonic()
  with log.open('xb') as f:p=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=None if argv[0]=='make' else 180)
  text=log.read_text(errors='replace');record['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));record.setdefault('private_evidence',[]).extend(capture(text,out,len(record['commands'])));print(text[-2400:],flush=True);return p.returncode,text
 try:
  env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());weights=pathlib.Path(env['mosaic_assets'])
  from scripts.unified.generate_mosaic_package import generate
  small=out/'input-small.f32';small.write_bytes(b''.join(struct.pack('<f',(i%37-18)*.025) for i in range(288)));model=out/'model-small';record['model']=generate(weights,weights.parent/'manifest.json',small,12,8,64,model)
  for p in model.iterdir():p.chmod(0o400)
  model.chmod(0o500);jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  targets=['test_native_daemon','test_native_export_bridge','vxgpud','mosaic-owner-catalog.bin'];code,_=run(['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']+[out/p for p in targets]+['-j'+str(jobs)]);assert code==0,'compile is not behavior RED'
  code,text=run([out/'test_native_daemon',model,out/'mosaic-owner-catalog.bin',selected_directory('.')/'code.bin',selected_directory('.')/'rodata.bin',out/'mosaic-export','export'])
  if mode=='red':assert code==1 and 'U20 sealed Mosaic file export must bind to actual installation' in text;record['functional_red']='PRODUCTION_MOSAIC_FILE_EXPORT_NOT_INSTALLED'
  else:
   assert code==0 and 'U20_INSTALLED_MOSAIC_RPC_FILE_EXPORT_AND_RETIREMENT_PASS_HOST_MARKER_NOT_INFERENCE_GPU_DP' in text
   source=next(x.split('=',1)[1] for x in text.splitlines() if x.startswith('NATIVE_INSTALLED_EXPORTED_MANIFEST='))
   from scripts.unified.texture_package import build_package
   package=out/'texture';metadata=build_package(source,package);record['texture_package_id']=metadata['package_id'];code,text=run([out/'test_native_export_bridge',source,package,metadata['package_id'],'installed']);assert code==0
  record['natural_exit']=0
 except BaseException as e:record.update(natural_exit=1,error=repr(e));raise
 finally:
  record['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()
