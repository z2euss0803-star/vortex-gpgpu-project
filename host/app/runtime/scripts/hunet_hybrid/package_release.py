"""Source-bound program8 alias and CPU contract. No board execution claims."""
import argparse,hashlib,importlib.util,json,re,shutil,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet'))
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
legacy=load('hybrid_legacy_package',ROOT/'scripts/hunet/package_release.py')
freeze=load('hybrid_freeze',ROOT/'scripts/hunet_hybrid/freeze.py')
sha=legacy.sha
def write(p,v):
 with p.open('w',encoding='utf-8',newline='\n') as f:f.write(json.dumps(v,sort_keys=True,indent=2,ensure_ascii=False)+'\n')
def cpu_elf_check(b):
 if len(b)<64 or b[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',b,18)[0]!=183:raise ValueError('CPU_AARCH64_ELF_REQUIRED')
def cpu_contract():
 return dict(schema=1,algorithm='ERF_NATIVE_V1',threads=4,dtype='FP32_LE',rounding='FE_TONEAREST',
 expression='(x*0.5f)*(1.0f+erff(x/1.4142135623730951f))',
 compile_flags=['-fno-fast-math','-ffp-contract=off'],thread_lifecycle='create_join_each_layer',
 max_elements=393216,overlap=False,finite_only=True)
def binding_bytes(base,cpu,contract,stages):
 old=base/'binding.bin';b=old.read_bytes()
 if len(b)!=256 or struct.unpack('<8I',b[:32])!=(0x44424c48,1,256,4,6,3,0,0):raise ValueError('OLD_BINDING_IDENTITY')
 return struct.pack('<8I',0x44424c48,2,384,4,8,7,0,0)+b[32:]+hashlib.sha256(cpu).digest()+hashlib.sha256(contract).digest()+hashlib.sha256(stages).digest()+bytes(32)
def package_hybrid(source_root,out,cpu_elf,test_fixture=False):
 root=Path(source_root);out=Path(out);cpu=Path(cpu_elf).read_bytes()
 if test_fixture:
  if not cpu.startswith(b'TEST_FIXTURE'):raise ValueError('TEST_FIXTURE_MARKER_REQUIRED')
 else:cpu_elf_check(cpu)
 if out.exists():raise ValueError('HYBRID_RELEASE_ALREADY_EXISTS')
 commit=legacy.git(root,'rev-parse','HEAD').decode().strip()
 if not test_fixture and commit==freeze.BASE:raise ValueError('NEW_SOURCE_IDENTITY_REQUIRED')
 out.mkdir(parents=True)
 r=legacy.package_release(root,out/'base',commit)
 role=freeze.freeze_contract(root);write(out/'cpu-contract.json',cpu_contract());write(out/'cpu-stages.json',role)
 (out/'cpu').mkdir();(out/'cpu/gelu.elf').write_bytes(cpu)
 shutil.copytree(out/'base/program6',out/'program8')
 alias=dict(program_id=8,content_alias=6,entry=0x80000000,wire_sha256=r['program_sha256'],crt='FROZEN_PROGRAM6_CRT',source_digest=r['source_digest'])
 write(out/'program8/alias.json',alias)
 binding=binding_bytes(out/'base',cpu,(out/'cpu-contract.json').read_bytes(),(out/'cpu-stages.json').read_bytes())
 (out/'binding-hybrid.bin').write_bytes(binding)
 header='#ifndef VXL_HYBRID_RELEASE_H\n#define VXL_HYBRID_RELEASE_H\nstatic const vxl_u8 vxl_hybrid_binding_bytes[384] __attribute__((used))={'+','.join('0x%02x'%v for v in binding)+'};\n#endif\n'
 (out/'build').mkdir();(out/'build/vxl_hybrid_release.h').write_bytes(header.encode())
 records={p.relative_to(out).as_posix():dict(bytes=p.stat().st_size,sha256=sha(p.read_bytes())) for p in sorted(out.rglob('*')) if p.is_file()}
 result=dict(schema=1,scope='TEST_FIXTURE_HYBRID_NOT_CROSS' if test_fixture else 'HUNET_HYBRID_OFFLINE_RELEASE',
 source_commit=r['source_commit'],source_digest=r['source_digest'],baseline_commit=freeze.BASE,
 profile=4,program_id=8,binding_sha256=sha(binding),cpu_binary_sha256=sha(cpu),files=records,
 statuses=dict(OFFLINE='PENDING',CROSS='NOT_RUN',IMAGE='NOT_RUN',HW4_MIXED='NOT_RUN',H1='NOT_RUN'))
 write(out/'release-hybrid.json',result);return result
def verify_hybrid_release(path):
 try:
  root=Path(path);r=json.loads(legacy.file_bytes(root,'release-hybrid.json'))
  if r.get('schema')!=1 or r.get('scope') not in ['HUNET_HYBRID_OFFLINE_RELEASE','TEST_FIXTURE_HYBRID_NOT_CROSS'] or r.get('program_id')!=8 or r.get('profile')!=4:raise ValueError('HYBRID_SCHEMA')
  for name,v in r['files'].items():
   b=legacy.file_bytes(root,name)
   if len(b)!=v['bytes'] or sha(b)!=v['sha256']:raise ValueError('HYBRID_FILE_SHA:'+name)
  actual={p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() or p.is_symlink()}
  if actual!=set(r['files'])|{'release-hybrid.json'}:raise ValueError('HYBRID_UNLISTED_FILE')
  checked=legacy.verify_release(root/'base')
  if checked['integrity']!='PASS':raise ValueError('BASE:'+checked.get('reason',''))
  if checked['source_commit']!=r['source_commit'] or checked['source_digest']!=r['source_digest']:raise ValueError('HYBRID_SOURCE')
  import tarfile
  with tarfile.open(root/'base/source.tar.gz','r:gz') as archive:archived_model=json.loads(archive.extractfile('contracts/hunet_v5.json').read())
  role=json.loads((root/'cpu-stages.json').read_bytes());freeze.verify_baseline(role,archived_model)
  contract=(root/'cpu-contract.json').read_bytes();stages=(root/'cpu-stages.json').read_bytes()
  if json.loads(contract)!=cpu_contract():raise ValueError('CPU_CONTRACT_CHANGED')
  cpu=(root/'cpu/gelu.elf').read_bytes()
  if r['scope']=='HUNET_HYBRID_OFFLINE_RELEASE':
   cpu_elf_check(cpu)
   if r['source_commit']==freeze.BASE:raise ValueError('NEW_SOURCE_REQUIRED')
  elif not cpu.startswith(b'TEST_FIXTURE'):raise ValueError('FIXTURE_SCOPE')
  if sha(cpu)!=r['cpu_binary_sha256']:raise ValueError('CPU_SHA')
  binding=binding_bytes(root/'base',cpu,contract,stages)
  if (root/'binding-hybrid.bin').read_bytes()!=binding or sha(binding)!=r['binding_sha256']:raise ValueError('HYBRID_BINDING')
  for p in (root/'base/program6').iterdir():
   if p.read_bytes()!=(root/'program8'/p.name).read_bytes():raise ValueError('PROGRAM8_ALIAS:'+p.name)
  alias=json.loads((root/'program8/alias.json').read_bytes())
  if alias!=dict(program_id=8,content_alias=6,entry=0x80000000,wire_sha256=sha((root/'program8/code.bin').read_bytes()),crt='FROZEN_PROGRAM6_CRT',source_digest=r['source_digest']):raise ValueError('ALIAS_IDENTITY')
  return dict(integrity='PASS',scope=r['scope'],source_commit=r['source_commit'],source_digest=r['source_digest'],binding_sha256=r['binding_sha256'],board='NOT_RUN')
 except (ValueError,KeyError,TypeError,OSError,struct.error) as e:return dict(integrity='FAIL',reason=str(e))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=ROOT);p.add_argument('--out',type=Path);p.add_argument('--cpu-elf',type=Path);p.add_argument('--verify',type=Path);a=p.parse_args()
 r=verify_hybrid_release(a.verify) if a.verify else package_hybrid(a.source,a.out,a.cpu_elf)
 print(json.dumps({k:v for k,v in r.items() if k!='files'},indent=2));raise SystemExit(1 if r.get('integrity')=='FAIL' else 0)
