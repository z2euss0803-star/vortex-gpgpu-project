"""Run inside the existing B/C resource gate. Host results are never A53/board."""
import hashlib,json,pathlib,re,struct,subprocess,sys,time
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def main():
 name=sys.argv[1];mode=sys.argv[2];assert mode in ('normal','sanitize','thread','cross')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 record=dict(name=name,mode=mode,scope='UBUNTU_HOST_NOT_A53' if mode!='cross' else 'MATCHED_AARCH64_LINK_NOT_EXECUTED',gpu_raw_cycles=None,commands=[],artifacts=[])
 def run(argv):
  start=time.monotonic();p=subprocess.run([str(x) for x in argv],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  log=out/('command-%03d.log'%len(record['commands']));log.write_bytes(p.stdout);record['commands'].append(dict(argv=[str(x) for x in argv],natural_exit=p.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n');print(p.stdout.decode('utf8','replace')[-4000:],flush=True)
  if p.returncode:raise RuntimeError(str(log)+' exit='+str(p.returncode))
  return p.stdout.decode('utf8','replace')
 try:
  meta=json.loads(pathlib.Path('docs/unified/U08-gelu-inputs.json').read_text());inputs=pathlib.Path(meta['target']);assert sha(inputs/'inputs-manifest.json')==meta['manifest_sha256']
  for row in meta['files']:
   p=inputs/row['path'];assert p.is_file() and not p.is_symlink() and not p.stat().st_mode&0o222 and p.stat().st_nlink==1 and p.stat().st_size==row['bytes'] and sha(p)==row['sha256']
  record['input_manifest_sha256']=meta['manifest_sha256'];record['input_files_verified']=len(meta['files'])
  command=['make','-f','Makefile.unified','OUT='+str(out/'objects')]
  if mode in ('sanitize','thread'):command+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];record['compiler']=run([prefix+'g++','--version']);assert '9.2.0' in record['compiler'];assert run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   command+=['CXX='+prefix+'g++',str(out/'objects/test_cpu_cpu'),str(out/'objects/test_cpu_gelu'),'-j1'];run(command);needed=set();versions=set()
   for n in ('test_cpu_cpu','test_cpu_gelu'):
    p=out/'objects'/n;raw=p.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==183
    text=run([prefix+'readelf','-hW','-dW','-VW',p]);(out/(n+'.readelf')).write_text(text);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);record['target_runtime']=dict(rootfs_sha256=sha(env['rootfs']),files=files,needed=sorted(needed),required_versions=sorted(versions))
  else:
   run(command+['test-unified-cpu','test-unified-gelu','-j1']);outputs=out/'replay';outputs.mkdir();text=run([out/'objects/test_cpu_gelu',inputs,outputs]);rows=[json.loads(s) for s in text.splitlines() if s.startswith('{')];assert len(rows)==13 and sum(x['elements'] for x in rows)==1869056 and all(x['bit_diff_scalar']==0 and x['gpu_raw_cycles'] is None for x in rows)
   record['replay']=dict(scope='UBUNTU_HOST_NOT_TARGET_NOT_NETWORK',rows=rows,total_elements=1869056,total_bit_diff_old_a53=sum(x['bit_diff_old_a53'] for x in rows),total_bit_diff_gpu_reference=sum(x['bit_diff_gpu_reference'] for x in rows));(out/'replay-summary.json').write_text(json.dumps(record['replay'],indent=2)+'\n')
   for p in outputs.iterdir():record['artifacts'].append(dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p)))
  for p in (out/'objects').iterdir():
   if p.is_file():record['artifacts'].append(dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p)))
  record['natural_exit']=0;print('U08_'+mode.upper()+'_PASS_GPU_CYCLES_NULL',flush=True)
 except Exception as e:record.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
 return 0
if __name__=='__main__':sys.exit(main())
