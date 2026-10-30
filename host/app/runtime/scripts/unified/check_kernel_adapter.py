"""Early compile of U05 Linux adapters; NOT a fully bound U23 image/module."""
import hashlib,json,pathlib,shutil,struct,subprocess,sys

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
 root=pathlib.Path.cwd();out=root/'build'/sys.argv[1];out.mkdir(parents=True,exist_ok=False)
 env=pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json')
 data=json.loads(env.read_text());kernel=pathlib.Path(data['kernel_source']);kb=pathlib.Path(data['kernel_build']);prefix=data['aarch64_prefix']
 release=(kb/'include/config/kernel.release').read_text().strip();assert release.startswith('5.4.0-xilinx-v2020.1')
 before={str(p):sha(p) for p in (kb/'.config',kb/'Module.symvers')}
 private=out/'source';private.mkdir()
 for name in ('driver','core','include','generated'):
  shutil.copytree(str(root/name),str(private/name),ignore=shutil.ignore_patterns('*.o','*.ko','*.cmd','*.pyc','__pycache__','Module.symvers','modules.order'))
 command=['make','-j1','-C',str(kernel),'O='+str(kb),'ARCH=arm64','CROSS_COMPILE='+prefix,'M='+str(private/'driver'),'W=1','modules']
 stage=sys.argv[2] if len(sys.argv)>2 else 'U05'
 record=dict(scope='PRE_BINDING_'+stage+'_ADAPTER_AARCH64_MODULE_NOT_RELEASE_NOT_IMAGE_NOT_BOARD',command=command,kernel_release=release,parent_inputs=before,gpu_raw_cycles=None)
 print(json.dumps(record),flush=True)
 code=subprocess.call(command);record['natural_exit']=code
 if code==0:
  module=private/'driver/vxl_linux.ko';binary=module.read_bytes();assert binary[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',binary,18)[0]==183
  nm=subprocess.check_output([prefix+'nm','--defined-only',str(module)]).decode()
  required=['vxl_v2_dispatch','vxl_v2_linux_bind','vxl_v2_worker_locked','vxl_v2_linux_poll','vxl_v2_close_locked']
  if stage=='U06':required+=['vxl_v2_control','vxl_v2_s8_admit','vxl_v2_p8_admit','vxl_v2_native_memory_ops','vxl_v2_boot_bind','vxl_v2_linux_startup','vxl_v2_execution_snapshot','vxl_v2_native_execution_ops','vxl_v2_observe']
  for symbol in required:
   assert any(line.split()[-1:]==[symbol] for line in nm.splitlines()),symbol
  (out/'module.nm').write_text(nm)
  record.update(module_sha256=sha(module),module_bytes=len(binary))
 assert all(sha(pathlib.Path(p))==h for p,h in before.items()),'parent kernel inputs changed'
 (out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record));return code

if __name__=='__main__':sys.exit(main())
