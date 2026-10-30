"""Committed full source, immutable old programs, new program9 and actual CPU binding."""
import argparse,gzip,hashlib,importlib.util,io,json,os,pathlib,re,shutil,struct,subprocess,sys,tarfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet'))
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
legacy=load('fused_base_package',ROOT/'scripts/hunet/package_release.py')
hybrid=load('fused_cpu_contract',ROOT/'scripts/hunet_hybrid/package_release.py')
sha=legacy.sha;canonical=legacy.canonical
def write(p,v):p.write_bytes((json.dumps(v,sort_keys=True,indent=2)+'\n').encode())
def git(root,*args):
 exe='F:/git/Git/cmd/git.exe' if os.name=='nt' else '/mnt/f/git/Git/cmd/git.exe'
 cwd=str(root) if os.name=='nt' else subprocess.check_output(['wslpath','-w',str(root)]).decode().strip()
 return subprocess.check_output([exe,'-c','safe.directory='+cwd,'-c','core.quotepath=false','-C',cwd,*args])
legacy.git=git
def source_inventory(root):
 root=pathlib.Path(root).resolve();commit=git(root,'rev-parse','HEAD').decode().strip()
 if git(root,'diff','--name-only','HEAD').strip():raise ValueError('DIRTY_SOURCE')
 files={}
 for row in git(root,'ls-tree','-r',commit).decode().splitlines():
  info,name=row.split('\t',1);mode,kind,blob=info.split()
  if name not in legacy.TOP and name.split('/')[0] not in legacy.PREFIXES|{'device'}:continue
  b=legacy.file_bytes(root,name)
  if mode not in ('100644','100755') or kind!='blob' or hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()!=blob:raise ValueError('SOURCE_COMMIT_BYTES:'+name)
  files[name]=dict(bytes=len(b),sha256=sha(b),git_blob=blob)
 return dict(schema=1,source_commit=commit,files=files)
def archive_source(root,inventory,out):
 with pathlib.Path(out).open('xb') as fp:
  with gzip.GzipFile(fileobj=fp,mode='wb',mtime=0,filename='') as gz:
   with tarfile.open(fileobj=gz,mode='w') as tar:
    for name,v in sorted(inventory['files'].items()):
     b=legacy.file_bytes(pathlib.Path(root),name)
     if sha(b)!=v['sha256']:raise ValueError('SOURCE_CHANGED')
     t=tarfile.TarInfo(name);t.size=len(b);t.mode=0o644;t.mtime=0;tar.addfile(t,io.BytesIO(b))
def verify_source(root,inventory):
 for name,v in inventory['files'].items():
  b=legacy.file_bytes(pathlib.Path(root),name)
  if len(b)!=v['bytes'] or sha(b)!=v['sha256'] or hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()!=v['git_blob']:raise ValueError('SOURCE_IDENTITY:'+name)
def verify_payload(root,records,manifest=None):
 for name,v in records.items():
  b=legacy.file_bytes(pathlib.Path(root),name)
  if len(b)!=v['bytes'] or sha(b)!=v['sha256']:raise ValueError('BOUND_ASSET:'+name)
 actual={p.relative_to(root).as_posix() for p in pathlib.Path(root).rglob('*') if p.is_file() or p.is_symlink()}
 if actual!=set(records)|({manifest} if manifest else set()):raise ValueError('UNLISTED_ASSET')
def binding(base,source_digest,program,cpu,contract,stages,root):
 b=(base/'binding.bin').read_bytes()
 hashes=[b[32:64],bytes.fromhex(program['wire_sha256']),bytes.fromhex(source_digest),b[128:160],hashlib.sha256((root/'jobs.bin').read_bytes()).digest(),b[192:256]]
 return struct.pack('<8I',0x44424c48,3,512,4,9,15,0,0)+b''.join(hashes)+hashlib.sha256(cpu).digest()+hashlib.sha256(contract).digest()+hashlib.sha256(stages).digest()+bytes(32)+b''.join(hashlib.sha256((root/n).read_bytes()).digest() for n in ('fusion-contract.json','descriptors.bin','layout.json'))+bytes(32)
def release_header(b,p):
 raw=struct.pack('<2I',p['wire_bytes'],p['immutable_spans'][0]['bytes'])+bytes.fromhex(p['wire_sha256'])+bytes.fromhex(p['immutable_spans'][0]['sha256'])+b+hashlib.sha256(b).digest()
 if len(raw)!=616:raise ValueError('RELEASE_STRUCT_SIZE')
 return ('#ifndef VXL_FUSED_RELEASE_H\n#define VXL_FUSED_RELEASE_H\nstatic const union {struct vxl_fused_release value;vxl_u8 raw[616];} vxl_fused_release_union __attribute__((used))={.raw={'+','.join('0x%02x'%x for x in raw)+'}};\n#define vxl_fused_release_bound (vxl_fused_release_union.value)\n#endif\n').encode()
def package_release(root,rv32,out,cpu_dir):
 root,rv32,out,cpu_dir=map(pathlib.Path,(root,rv32,out,cpu_dir));inventory=source_inventory(root);commit=inventory['source_commit']
 if out.exists():raise ValueError('FRESH_RELEASE_REQUIRED')
 cpu_receipt=json.loads((cpu_dir/'receipt.json').read_bytes())
 load('fused_package_cpu_receipt',root/'scripts/hunet_fused/cross.py').checked_cpu(cpu_dir,cpu_receipt)
 if cpu_receipt.get('scope')!='FUSED_CPU_AARCH64_BUILD' or cpu_receipt.get('exit')!=0 or cpu_receipt.get('source_commit')!=commit or cpu_receipt.get('source_digest')!=sha(canonical(inventory)):raise ValueError('ACTUAL_SAME_SOURCE_CPU_REQUIRED')
 cpu=(cpu_dir/'objects/vxl_hunet_fused').read_bytes();hybrid.cpu_elf_check(cpu)
 if sha(cpu)!=cpu_receipt['elfs']['vxl_hunet_fused']['sha256']:raise ValueError('CPU_BUILD_IDENTITY')
 static=load('fused_rv32_release_audit',root/'scripts/hunet_fused/audit_rv32.py').audit(rv32)
 program=json.loads((rv32/'program.json').read_bytes());out.mkdir(parents=True)
 legacy.package_release(root,out/'base',commit)
 write(out/'SOURCE_SHA256.json',inventory);source_digest=sha(canonical(inventory));archive_source(root,inventory,out/'source.tar.gz')
 (out/'cpu').mkdir();(out/'cpu/gelu.elf').write_bytes(cpu);shutil.copyfile(cpu_dir/'objects/libvortex-fused.so',out/'cpu/libvortex-fused.so')
 write(out/'cpu/build.json',cpu_receipt);write(out/'cpu/runtime.json',cpu_receipt['cpu_runtime'])
 for name in ('cli.map','build.log','build.log.exit'):shutil.copyfile(cpu_dir/name,out/'cpu'/name)
 (out/'cpu/libm.sha256').write_bytes((cpu_receipt['cpu_runtime']['libm_sha256']+'\n').encode())
 write(out/'cpu-contract.json',hybrid.cpu_contract());write(out/'cpu-stages.json',hybrid.freeze.freeze_contract(root))
 shutil.copytree(out/'base/program6',out/'program8')
 for name,source in [('jobs.bin','generated/hunet_fused/jobs.bin'),('descriptors.bin','generated/hunet_fused/descriptors.bin'),('layout.json','generated/hunet_fused/layout.json'),('schedule.json','generated/hunet_fused/schedule.json'),('fusion-contract.json','contracts/hunet_fused_v1.json')]:shutil.copyfile(root/source,out/name)
 (out/'program9').mkdir()
 for n in ('kernel.elf','code.bin','kernel.map','kernel.disasm','kernel.headers','program.json','build.json','dependencies.sha256','audit.json'):shutil.copyfile(rv32/n,out/'program9'/n)
 b=binding(out/'base',source_digest,program,cpu,(out/'cpu-contract.json').read_bytes(),(out/'cpu-stages.json').read_bytes(),out)
 (out/'binding-fused.bin').write_bytes(b);(out/'build').mkdir();(out/'build/vxl_fused_release.h').write_bytes(release_header(b,program))
 # Legacy paths retain their exact GPU bytes and a current-core source header.
 old_cpu=(ROOT.parent/'vortex_linux_backend_hunet_hybrid_20261013/release/hybrid-7b8fc45/cpu/gelu.elf').read_bytes()
 old=hybrid.binding_bytes(out/'base',old_cpu,(out/'cpu-contract.json').read_bytes(),(out/'cpu-stages.json').read_bytes())
 (out/'build/vxl_hybrid_release.h').write_bytes(('#ifndef VXL_HYBRID_RELEASE_H\n#define VXL_HYBRID_RELEASE_H\nstatic const vxl_u8 vxl_hybrid_binding_bytes[384] __attribute__((used))={'+','.join('0x%02x'%x for x in old)+'};\n#endif\n').encode())
 files={p.relative_to(out).as_posix():dict(bytes=p.stat().st_size,sha256=sha(p.read_bytes())) for p in sorted(out.rglob('*')) if p.is_file()}
 base=json.loads((out/'base/release.json').read_bytes())
 result=dict(schema=1,scope='HUNET_FUSED_OFFLINE_RELEASE',source_commit=commit,source_digest=source_digest,profile=4,program_id=9,binding_sha256=sha(b),bit_sha256=base['bit_sha256'],xsa_sha256=base['xsa_sha256'],files=files,deployable=False,statuses=dict(HOST='PENDING_FINAL',RTL='WAIT_RTL',CROSS='CPU_ONLY',IMAGE='NOT_RUN',HW4_FUSED='NOT_RUN',H1='NOT_RUN'))
 write(out/'release-fused.json',result);return result
def verify_fused_release(path):
 try:
  root=pathlib.Path(path);r=json.loads(legacy.file_bytes(root,'release-fused.json'))
  if r.get('schema')!=1 or r.get('scope')!='HUNET_FUSED_OFFLINE_RELEASE' or r.get('profile')!=4 or r.get('program_id')!=9 or r.get('deployable') is not False:raise ValueError('FUSED_RELEASE_SCHEMA')
  verify_payload(root,r['files'],'release-fused.json');base=legacy.verify_release(root/'base')
  if base['integrity']!='PASS':raise ValueError('BASE:'+str(base))
  inventory=json.loads((root/'SOURCE_SHA256.json').read_bytes())
  if inventory['source_commit']!=r['source_commit'] or sha(canonical(inventory))!=r['source_digest']:raise ValueError('SOURCE_BINDING')
  seen=set();archived_model=None
  with tarfile.open(root/'source.tar.gz','r:gz') as archive:
   for t in archive:
    legacy.safe_name(t.name)
    if t.name in seen or not t.isfile() or t.name not in inventory['files']:raise ValueError('SOURCE_ARCHIVE_MEMBER')
    seen.add(t.name);b=archive.extractfile(t).read();v=inventory['files'][t.name]
    if len(b)!=v['bytes'] or sha(b)!=v['sha256'] or hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()!=v['git_blob']:raise ValueError('SOURCE_ARCHIVE_HASH')
    if t.name=='contracts/hunet_v5.json':archived_model=json.loads(b)
  if seen!=set(inventory['files']) or 'device/hunet_fused.cpp' not in seen:raise ValueError('FULL_SOURCE_COVERAGE')
  for name,source in [('jobs.bin','generated/hunet_fused/jobs.bin'),('descriptors.bin','generated/hunet_fused/descriptors.bin'),('layout.json','generated/hunet_fused/layout.json'),('schedule.json','generated/hunet_fused/schedule.json'),('fusion-contract.json','contracts/hunet_fused_v1.json')]:
   if sha((root/name).read_bytes())!=inventory['files'][source]['sha256']:raise ValueError('SOURCE_BOUND_GENERATED_ASSET:'+name)
  for name,want in json.loads((root/'program9/build.json').read_bytes())['sources'].items():
   if name not in inventory['files'] or inventory['files'][name]['sha256']!=want:raise ValueError('RV32_COMPILED_SOURCE:'+name)
  program=json.loads((root/'program9/program.json').read_bytes())
  from audit_program import audit_program
  audit_program(root/'program9/kernel.elf',root/'program9/code.bin',program)
  cpu=(root/'cpu/gelu.elf').read_bytes();hybrid.cpu_elf_check(cpu)
  cpu_build=json.loads((root/'cpu/build.json').read_bytes());runtime=json.loads((root/'cpu/runtime.json').read_bytes())
  load('fused_verify_cpu_receipt',ROOT/'scripts/hunet_fused/cross.py').checked_cpu(root/'cpu',cpu_build,packaged=True)
  if cpu_build.get('scope')!='FUSED_CPU_AARCH64_BUILD' or cpu_build.get('exit')!=0 or cpu_build.get('source_commit')!=r['source_commit'] or cpu_build.get('source_digest')!=r['source_digest'] or cpu_build['elfs']['vxl_hunet_fused']['sha256']!=sha(cpu) or cpu_build['cpu_runtime']!=runtime or (root/'cpu/libm.sha256').read_bytes()!=(runtime['libm_sha256']+'\n').encode():raise ValueError('CPU_BUILD_RUNTIME_BINDING')
  library=(root/'cpu/libvortex-fused.so').read_bytes();hybrid.cpu_elf_check(library)
  if sha(library)!=cpu_build['elfs']['libvortex-fused.so']['sha256']:raise ValueError('CPU_LIBRARY_BUILD_BINDING')
  if json.loads((root/'cpu-contract.json').read_bytes())!=hybrid.cpu_contract():raise ValueError('CPU_ALGORITHM_CHANGED')
  if archived_model is None:raise ValueError('SOURCE_BOUND_MODEL_REQUIRED')
  hybrid.freeze.verify_baseline(json.loads((root/'cpu-stages.json').read_bytes()),archived_model)
  expected=binding(root/'base',r['source_digest'],program,cpu,(root/'cpu-contract.json').read_bytes(),(root/'cpu-stages.json').read_bytes(),root)
  if (root/'binding-fused.bin').read_bytes()!=expected or sha(expected)!=r['binding_sha256'] or (root/'build/vxl_fused_release.h').read_bytes()!=release_header(expected,program):raise ValueError('FUSED_BINDING')
  for p in (root/'base/program6').iterdir():
   if p.read_bytes()!=(root/'program8'/p.name).read_bytes():raise ValueError('PROGRAM8_BYTES_CHANGED')
  if any(r[k]!=json.loads((root/'base/release.json').read_bytes())[k] for k in ('bit_sha256','xsa_sha256')):raise ValueError('HARDWARE_CHANGED')
  return dict(integrity='PASS',scope=r['scope'],source_commit=r['source_commit'],source_digest=r['source_digest'],binding_sha256=r['binding_sha256'],deployable=False,board='NOT_RUN')
 except (ValueError,KeyError,TypeError,OSError,struct.error,tarfile.TarError) as e:return dict(integrity='FAIL',reason=str(e),deployable=False)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',type=pathlib.Path,default=ROOT);p.add_argument('--rv32',type=pathlib.Path);p.add_argument('--cpu-dir',type=pathlib.Path);p.add_argument('--out',type=pathlib.Path);p.add_argument('--verify',type=pathlib.Path);a=p.parse_args()
 r=verify_fused_release(a.verify) if a.verify else package_release(a.root,a.rv32,a.out,a.cpu_dir)
 print(json.dumps({k:v for k,v in r.items() if k!='files'},indent=2));raise SystemExit(1 if r.get('integrity')=='FAIL' else 0)
