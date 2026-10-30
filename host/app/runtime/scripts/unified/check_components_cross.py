"""Component compile only; final provider/ELF/image binding is still U22/U23."""
import hashlib,json,pathlib,struct,subprocess,sys

def main():
 root=pathlib.Path.cwd();name=sys.argv[1];out=root/'build'/name;out.mkdir(parents=True,exist_ok=False)
 env=pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json');data=json.loads(env.read_text());prefix=data['aarch64_prefix']
 target=out/'compute_v2.o';command=[prefix+'g++','-std=c++17','-O2','-g','-Wall','-Wextra','-Werror','-fPIC','-pthread','-Iinclude','-c','nnrt/backend/compute_v2.cpp','-o',str(target)]
 print(json.dumps(dict(command=command,scope='AARCH64_COMPONENT_OBJECT_ONLY_NOT_SERVICE_NOT_IMAGE_NOT_BOARD',gpu_raw_cycles=None)),flush=True)
 rc=subprocess.call(command)
 if rc:return rc
 raw=target.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==183
 receipt=dict(object_sha256=hashlib.sha256(raw).hexdigest(),object_bytes=len(raw),natural_exit=0,toolchain_prefix=prefix,gpu_raw_cycles=None)
 (out/'cpp-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt),flush=True)
 return subprocess.call([sys.executable,'scripts/unified/check_kernel_adapter.py',name+'-module','U06'])

if __name__=='__main__':sys.exit(main())
