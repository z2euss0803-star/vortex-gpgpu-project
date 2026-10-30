"""Serial matched joint release stages inside the existing three-lock runner.

No device/media APIs. Each stage has its own immutable output and receipt;
kernel/image retain the original fresh15GiB and worker1 resource gate.
"""
import argparse,json,pathlib,shutil,sys,hashlib,subprocess
from scripts.unified.cross_common import snapshot,sha,verify_source_lock

def encoded(value):return (json.dumps(value,sort_keys=True,separators=(',',':'))+'\n').encode()
def initialize(base_env,control):
 root=pathlib.Path('.').resolve();control=pathlib.Path(control).resolve()
 if root/'build' not in control.parents:raise ValueError('private joint release control under source/build required')
 control.mkdir(parents=True,exist_ok=False);base_env=pathlib.Path(base_env);old=json.loads(base_env.read_text());env=dict(old);env['root']=str(root)
 actual=snapshot(root);lock=control/'source-lock.json';lock.write_bytes(encoded(dict(schema=1,snapshot=actual)));lock.chmod(0o400);env.update(source_lock=str(lock),source_lock_sha256=sha(lock),base_environment=dict(path=str(base_env),sha256=sha(base_env)),worker_count=1)
 env['provider_hardware']=dict(old['provider_hardware']);env['provider_hardware']['4']=dict(old['provider_hardware']['1'])
 from scripts.unified.audit_kernel import read_hardware_input
 read_hardware_input(root,4,env['provider_hardware']['4']['xsa'],env['provider_hardware']['4']['bit'])
 # Reuse only the actual completed static JSON build receipt and archive,
 # preserving its original absolute input paths and full SHA verification.
 original=pathlib.Path(old['root'])/'build/U07-aarch64-012/receipt.json';dependency=json.loads(original.read_text());library=next(x for x in dependency['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'))
 if dependency.get('natural_exit')!=0 or sha(library['path'])!=library['sha256']:raise ValueError('actual target JSON dependency changed')
 target=root/'build/U07-aarch64-012/receipt.json';target.parent.mkdir(parents=True,exist_ok=False);target.write_bytes(original.read_bytes());env['json_dependency_origin']=dict(path=str(original),sha256=sha(original),library=library)
 env['hunet_weights']=old.get('hunet_weights','/home/z2euss/workspace/unified_gpu_runtime_20261021/inputs/hunet-674069f4.bin')
 if sha(env['hunet_weights'])!='674069f415d51ce502dd00e579125c0ca8fe6d7e71a71d08cfae457e293e39bc':raise ValueError('frozen HUNet weights differ')
 env['mosaic_input_f32']=old.get('mosaic_input_f32',old['mosaic_formal_input']);input_path=pathlib.Path(env['mosaic_input_f32']);input_sha=sha(input_path)
 if input_path.stat().st_size!=1280*960*12 or old.get('mosaic_input_sha256',input_sha)!=input_sha:raise ValueError('full frozen Mosaic input differs')
 env['mosaic_input_sha256']=input_sha
 tool=pathlib.Path(env['petalinux_settings']).parent/'components/yocto/buildtools/sysroots/x86_64-petalinux-linux/usr/bin/bootgen';env['bootgen_sha256']=sha(tool)
 path=control/'env.json';path.write_bytes(encoded(env));return path

def phase(name,env_path,control):
 from scripts.demo.image import resource_facts
 from scripts.unified.resources import resource_gate
 root=pathlib.Path('.').resolve();control=pathlib.Path(control).resolve();env_path=pathlib.Path(env_path);env=json.loads(env_path.read_text());verify_source_lock(root,env['source_lock'],env['source_lock_sha256'])
 record=dict(scope='MATCHED_JOINT_RELEASE_STAGE_NOT_BOARD',phase=name,provider_flags=4,gpu_raw_cycles=None,sd='NOT_TOUCHED',board='NOT_RUN')
 try:
  # Gate compilation at4GiB fresh RAM, independently of the outer runner's
  # lightweight upload gate. No active compiler exists before this call.
  record['resources']=resource_gate(resource_facts(root),'cross' if name=='cross' else 'unit')
  if name=='cross':
   from scripts.unified.build_rv32 import build_target as rv32
   from scripts.unified.build_userspace import build_target as aarch64
   rv=root/'build/joint-rv32';rv32(env_path,env['source_lock'],env['source_lock_sha256'],root,rv);env.update(rv32_receipt=str(rv/'receipt.json'),rv32_receipt_sha256=sha(rv/'receipt.json'));env_path.write_bytes(encoded(env))
   arm=root/'build/joint-aarch64';aarch64(env_path,env['source_lock'],env['source_lock_sha256'],root,'aarch64-s8-joint',arm);env.update(aarch64_receipt=str(arm/'receipt.json'),aarch64_receipt_sha256=sha(arm/'receipt.json'));env_path.write_bytes(encoded(env))
   probe=root/'build/joint-host-metadata';subprocess.check_call(['make','-f','Makefile.unified','OUT='+str(probe),'DEBUG_FLAGS=-g1',str(probe/'image_package_metadata'),'-j1'])
   env.update(image_metadata_probe=str(probe/'image_package_metadata'),image_metadata_probe_sha256=sha(probe/'image_package_metadata'));env_path.write_bytes(encoded(env));record['cross_receipts']={k:env[k] for k in ('rv32_receipt','rv32_receipt_sha256','aarch64_receipt','aarch64_receipt_sha256','image_metadata_probe_sha256')}
  elif name=='kernel':
   from scripts.unified.bind_kernel import build_kernel
   result=build_kernel(env_path,4,env['source_lock'],root/'build/joint-kernel');record['kernel_receipt_sha256']=sha(root/'build/joint-kernel/receipt.json')
  elif name=='payload':
   from scripts.unified.image import prepare_payload
   result=prepare_payload(env_path,4,root/'build/joint-kernel',root/'build/joint-payload');record['payload_receipt_sha256']=sha(root/'build/joint-payload/receipt.json')
  elif name=='image':
   from scripts.unified.image import prepare_image
   result=prepare_image(env_path,4,root/'build/joint-kernel',root/'build/joint-payload',root/'build/joint-image');record['image_receipt_sha256']=sha(root/'build/joint-image/receipt.json')
  elif name=='audit':
   from scripts.unified.audit_image import audit_image
   result=audit_image(env_path,root/'build/joint-kernel',root/'build/joint-payload',root/'build/joint-image/receipt.json',root/'build/joint-image-audit');record['audit_receipt_sha256']=sha(root/'build/joint-image-audit/receipt.json')
  else:raise ValueError('explicit release stage required')
  verify_source_lock(root,env['source_lock'],env['source_lock_sha256']);record['natural_exit']=0
 except BaseException as e:record.update(natural_exit=1,error=repr(e));raise
 finally:
  p=control/(name+'-stage-receipt.json')
  with p.open('xb') as f:f.write(encoded(record))
 return record
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('phase',choices=('init','cross','kernel','payload','image','audit'));p.add_argument('--base-env');p.add_argument('--control',default='build/joint-release-control');p.add_argument('--env');a=p.parse_args()
 if a.phase=='init':
  if not a.base_env:p.error('--base-env required for exact input binding')
  print(initialize(a.base_env,a.control))
 else:
  env=a.env or str(pathlib.Path(a.control)/'env.json');print(json.dumps(phase(a.phase,env,a.control),indent=2))
if __name__=='__main__':main()
