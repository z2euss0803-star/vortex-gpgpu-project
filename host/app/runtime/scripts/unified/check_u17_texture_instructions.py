import hashlib,importlib.util,json,os,pathlib,subprocess,sys,time,struct,re
from scripts.unified.texture_pillow import dependency
from scripts.unified.texture_package import build_package

def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal','sanitize','thread','cross');out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='FROZEN_RV32_CUSTOM_TEXTURE_INSTRUCTION_MODEL_NOT_GPU_RTL_DP',gpu_raw_cycles=None,commands=[])
 def run(argv,expected=0):
  begin=time.monotonic();p=subprocess.run(list(map(str,argv)),stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=list(map(str,argv)),natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=hashlib.sha256(p.stdout).hexdigest()));print(p.stdout.decode('utf8','replace')[-3000:],flush=True);assert p.returncode==expected;return p.stdout
 try:
  rec['dependency']=dependency();spec=importlib.util.spec_from_file_location('texture_fixtures','tests/unified/test_texture_package.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);module.Package().fixture(out/'input');meta=json.loads((out/'input/result.json').read_bytes());values=[float(x*255/7-12) if c==0 else float(y*255/7+.75) if c==1 else float(((x+y)%8)*255/7) for c in range(3) for y in range(8) for x in range(8)];raw=b''.join(struct.pack('<f',v) for v in values);(out/'input/output.f32').write_bytes(raw);meta['artifacts'][0]['sha256']=hashlib.sha256(raw).hexdigest();(out/'input/result.json').write_bytes((json.dumps(meta,sort_keys=True)+'\n').encode());package=build_package(out/'input/result.json',out/'package');rec['package_id']=package['package_id'];elf=out/'test_p8_texture_instructions';cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('thread' if mode=='thread' else 'address,undefined')]
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_bytes());prefix=env['aarch64_prefix'];assert b'9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()==b'aarch64-xilinx-linux';dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_bytes());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and hashlib.sha256(pathlib.Path(lib['path']).read_bytes()).hexdigest()==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=hashlib.sha256(dep_path.read_bytes()).hexdigest(),library=lib);cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent)]
  run(cmd+[elf,'-j'+os.environ.get('VX_UNIFIED_JOBS','1')]);raw=elf.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==(183 if mode=='cross' else 62)
  if mode=='cross':
   info=run([prefix+'readelf','-hW','-dW','-VW',elf]).decode();needed=set(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',info));versions=set(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',info));sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=hashlib.sha256(pathlib.Path(env['rootfs']).read_bytes()).hexdigest(),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  else:
   text=run([elf,'data/p8',out/'package',package['package_id']],1 if mode=='red' else 0);assert (b'U17_TEXTURE_BIND_ASSERT controlled dualLOD package must be admitted' if mode=='red' else b'U17_FROZEN_RV32_CUSTOM_TEXTURE_3_FRAMES_AND_REVERSE_EXACT_PASS_HOST_NOT_GPU_RTL_DP') in text;assert not any(x in text for x in (b'ERROR: AddressSanitizer',b'WARNING: ThreadSanitizer',b'runtime error:'))
  rec.update(natural_exit=0,mode=mode,elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest())
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_bytes((json.dumps(rec,indent=2)+'\n').encode())
if __name__=='__main__':main()
