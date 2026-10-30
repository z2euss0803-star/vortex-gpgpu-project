"""Actual full NN package admission + all media constructors, HOST evidence.
P8 executes only the existing C CPU oracle/fake KMS fixture. NN never runs.
"""
import pathlib,json,subprocess,sys,time,os
from scripts.unified.cross_common import sha,snapshot
from scripts.unified.generate_hunet_package import generate as hunet
from scripts.unified.generate_mosaic_package import generate as mosaic
from scripts.unified.joint_firmware import generate as firmware
from scripts.unified.capture_native_private import capture

def main():
 out=pathlib.Path('build')/sys.argv[1];out.mkdir(parents=True,exist_ok=False);before=snapshot('.');env=json.loads(pathlib.Path('/home/z2euss/workspace/unified_gpu_runtime_20261021/u24-s8-native-deps-env-1420.json').read_text());record=dict(scope='ACTUAL_FULL_NN_PACKAGES_JOINT_INSTALL_ADMISSION_P8_HOST_C_ORACLE_NOT_NN_INFERENCE_OR_PHYSICAL_DP',commands=[],gpu_raw_cycles=None,source_before=before)
 def run(argv,environ=None,timeout=None):
  argv=list(map(str,argv));p=out/('command-%03d.log'%len(record['commands']));begin=time.monotonic()
  with p.open('xb') as f:r=subprocess.run(argv,env=environ,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
  text=p.read_text(errors='replace');record['commands'].append(dict(argv=argv,natural_exit=r.returncode,elapsed_s=time.monotonic()-begin,log=str(p),log_sha256=sha(p)));record.setdefault('private_evidence',[]).extend(capture(text,out,len(record['commands'])));print(text[-2500:],flush=True);(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
  if r.returncode:raise ValueError('actual full package/install gate failed: '+str(p))
  return text
 try:
  weights=env['mosaic_assets'];input_path=pathlib.Path(env.get('mosaic_input_f32',env['mosaic_formal_input']))
  if input_path.stat().st_size!=1280*960*12:raise ValueError('actual original full1280x960 input required')
  record['input']=dict(path=str(input_path),bytes=input_path.stat().st_size,sha256=sha(input_path))
  record['models']=dict(hunet=hunet('/home/z2euss/workspace/unified_gpu_runtime_20261021/inputs/hunet-674069f4.bin',out/'hunet',provider_flag=4),mosaic=mosaic(weights,pathlib.Path(weights).parent/'manifest.json',input_path,1280,960,64,out/'mosaic',provider_flag=4));record['firmware']=firmware(out/'firmware')
  for name in ('hunet','mosaic'):
   for p in (out/name).iterdir():p.chmod(0o400)
   (out/name).chmod(0o500)
  run(['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1',out/'test_installed_p8',out/'image_package_metadata','-j1'])
  rows=[]
  for kind in ('hunet','mosaic'):
   result=json.loads(run([out/'image_package_metadata',kind,out/kind,'/usr/share/vxgpu/models/'+kind]));record.setdefault('actual_prepare',{})[kind]=result;row=result['installed_model'];row['package']=str((out/kind).resolve());rows.append(row)
  manifest=out/'real-full-nn-installation-rows.json';manifest.write_text(json.dumps(rows,indent=2)+'\n');manifest.chmod(0o400);execution=dict(os.environ,HOST_JOINT_NN_MANIFEST=str(manifest.resolve()))
  run([out/'test_installed_p8',out/'firmware/catalog-v1.bin',out/'firmware/programs','data/p8',out/'joint-all-output','joint'],execution,600)
  record.update(natural_exit=0,source_after=snapshot('.'))
  if record['source_after']!=before:raise ValueError('actual source changed during full package admission')
 except BaseException as e:record.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()
