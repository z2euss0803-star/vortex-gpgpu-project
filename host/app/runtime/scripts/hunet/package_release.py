"""Seal a committed source tree and immutable workload bytes; SHA256 required."""
import argparse,gzip,hashlib,io,json,os,re,shutil,struct,subprocess,tarfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
PREFIXES={'core','driver','include','runtime','userspace','mock','tools','generated','scripts','tests','contracts','vendor','docs'}
TOP={'Makefile','README.md','.gitignore','.gitattributes'}
def sha(b):return hashlib.sha256(b).hexdigest()
def canonical(v):return json.dumps(v,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode('utf-8')
def safe_name(name):
 if not isinstance(name,str) or not name or '\\' in name or ':' in name or name.startswith('/') or any(s in ['','..','.'] for s in name.split('/')) or re.search(r'(^|[/_-])v6([/_.-]|$)',name,re.I):raise ValueError('UNSAFE_OR_V6_PATH:'+str(name))
 return name
def git(root,*args):
 if os.name=='nt':exe='F:/git/Git/cmd/git.exe';cwd=str(root)
 elif shutil.which('wslpath') and Path('/mnt/f/git/Git/cmd/git.exe').is_file():
  # Windows-created worktrees keep Windows gitdir paths. Use the actual Windows Git.
  exe='/mnt/f/git/Git/cmd/git.exe';cwd=str(root)
 else:exe='git';cwd=str(root)
 target=subprocess.check_output(['wslpath','-w',str(root)]).decode().strip() if exe.startswith('/mnt/f/') else str(root)
 command=[exe,'-c','core.quotepath=false','-C',target,*args]
 r=subprocess.run(command,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 if r.returncode:raise ValueError('GIT:'+r.stderr.decode('utf-8',errors='replace'))
 return r.stdout
def write_json(p,v):p.write_text(json.dumps(v,indent=2,ensure_ascii=False)+'\n',encoding='utf-8',newline='\n')
def file_bytes(root,name):
 safe_name(name);p=root
 for part in name.split('/'):
  p=p/part
  if p.is_symlink():raise ValueError('SYMLINK:'+name)
 if not p.is_file():raise ValueError('MISSING:'+name)
 return p.read_bytes()
def package_release(source_root:Path,out_dir:Path,source_commit:str)->dict:
 root=source_root.resolve();out=out_dir.resolve()
 if not re.fullmatch('[0-9a-f]{40}',source_commit) or git(root,'rev-parse','HEAD').decode().strip()!=source_commit:raise ValueError('UNCOMMITTED_SOURCE_IDENTITY')
 if git(root,'diff','--name-only','HEAD').strip():raise ValueError('DIRTY_TRACKED_SOURCE')
 for name in git(root,'ls-files','--others','--exclude-standard').decode('utf-8').splitlines():
  if not name.startswith(('evidence/','release/','.superpowers/')):raise ValueError('UNTRACKED_SOURCE:'+name)
 files={};payload={}
 for record in git(root,'ls-tree','-r',source_commit).decode('utf-8').splitlines():
  info,name=record.split('\t',1);mode,kind,blob=info.split()
  if name not in TOP and name.split('/')[0] not in PREFIXES:continue
  safe_name(name)
  if mode not in ['100644','100755'] or kind!='blob':raise ValueError('SOURCE_NONREGULAR:'+name)
  b=file_bytes(root,name);actual_blob=hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()
  if actual_blob!=blob:raise ValueError('COMMIT_BYTES:'+name)
  files[name]=dict(bytes=len(b),sha256=sha(b),git_blob=blob);payload[name]=b
 deps=file_bytes(root,'build/hunet-rv32/dependencies.sha256').decode('utf-8')
 if len(deps.splitlines())<5 or any(not re.match('[0-9a-f]{64}  .+',s) for s in deps.splitlines()):raise ValueError('RV32_DEPENDENCY_SHA_REQUIRED')
 segments=json.loads(payload['generated/hunet/segments.json'])
 for sub,index in [('',0),('wmma/',1)]:
  from audit_program import audit_program
  path=root/'build/hunet-rv32'/sub
  audit_program(path/'kernel.elf',path/'code.bin',segments[index])
  if sha((path/'code.bin').read_bytes())!=segments[index]['wire_sha256']:raise ValueError('PROGRAM_IDENTITY')
 inventory=dict(schema=1,source_commit=source_commit,files=files,rv32_dependencies_sha256=sha(deps.encode()),programs=segments)
 source_digest=sha(canonical(inventory));m=json.loads(payload['contracts/hunet_v5.json']);profile=json.loads(payload['contracts/profiles/hw4-hunet-v5.json']);inputs=json.loads(payload['contracts/hunet_inputs.json'])['records']
 if out.exists():raise ValueError('RELEASE_ALREADY_EXISTS')
 out.mkdir(parents=True);write_json(out/'SOURCE_SHA256.json',inventory)
 with (out/'source.tar.gz').open('wb') as fp:
  with gzip.GzipFile(fileobj=fp,mode='wb',mtime=0,filename='') as gz:
   with tarfile.open(fileobj=gz,mode='w') as tar:
    for name,b in sorted(payload.items()):
     t=tarfile.TarInfo(name);t.size=len(b);t.mode=0o644;t.mtime=0;tar.addfile(t,io.BytesIO(b))
 def save(name,b):p=out/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
 vault=root.parents[1];refs={'v5':{}}
 def frozen(role,name,predicate=lambda f:True):
  f=next(f for f in m['files'] if f['role']==role and predicate(f));b=(vault/f['path']).read_bytes()
  if len(b)!=f['bytes'] or sha(b)!=f['sha256']:raise ValueError('FROZEN_ASSET:'+f['path'])
  save(name,b);return dict(path=name,bytes=len(b),sha256=sha(b))
 weights=frozen('weights','weights.bin');jobs=frozen('jobs_file','jobs.bin');aux=frozen('aux_file','matrix_descriptors.bin');model=frozen('onnx','model.onnx')
 frozen('bit','hardware/top.bit');frozen('xsa','hardware/gpgpu_with_bit.xsa')
 for role,name in [('original_acceptance','references/acceptance.json'),('original_R2','references/R2.npy'),('original_ORT_FP32','references/ORT.npy')]:refs[role]=frozen(role,name)
 for roi in [0,26]:refs['v5'][str(roi)]=frozen('logits_payload','references/roi%d.bare.f32'%roi,lambda f,roi=roi:'/roi%d/'%roi in f['path'])
 for r in inputs:
  if not r['included']:continue
  b=(vault/r['path']).read_bytes()
  if len(b)!=r['bytes'] or sha(b)!=r['sha256']:raise ValueError('INPUT_CHANGED')
  save('inputs/%03d.f32'%r['roi_index'],b)
 for sub,prefix,index in [('', 'program6',0),('wmma/','program7',1)]:
  for name in ['code.bin','kernel.elf','kernel.vxbin','program.json','kernel.map','kernel.disasm','kernel.headers']:
   save(prefix+'/'+name,(root/'build/hunet-rv32'/sub/name).read_bytes())
 for case in range(2):
  for name in ['args.bin','matrix.bin','a.f16','b.f16','expected.f32']:save('wmma/case%d/%s'%(case,name),(root/'build/hunet-rv32/wmma'/('case%d'%case)/name).read_bytes())
 save('rv32-dependencies.sha256',deps.encode());save('rv32-source-dependencies.sha256',(root/'build/hunet-rv32/source-dependencies.sha256').read_bytes());save('references.json',canonical(refs)+b'\n')
 hashes=[sha(payload['contracts/profiles/hw4-hunet-v5.json']),segments[0]['wire_sha256'],source_digest,weights['sha256'],jobs['sha256'],aux['sha256'],model['sha256']]
 binding=struct.pack('<8I',0x44424c48,1,256,4,6,3,0,0)+b''.join(bytes.fromhex(h) for h in hashes);save('binding.bin',binding)
 header='#ifndef VXL_HUNET_SOURCE_H\n#define VXL_HUNET_SOURCE_H\nstatic const volatile vxl_u8 vxl_hunet_source_sha[32] __attribute__((used))={'+','.join('0x%02x'%v for v in bytes.fromhex(source_digest))+'};\nstatic const char vxl_hunet_source_commit[] __attribute__((used))="'+source_commit+'";\n#endif\n';save('build/vxl_hunet_source.h',header.encode())
 records={p.relative_to(out).as_posix():dict(bytes=p.stat().st_size,sha256=sha(p.read_bytes())) for p in sorted(out.rglob('*')) if p.is_file()}
 release=dict(schema=1,scope='HUNET_OFFLINE_RELEASE',source_commit=source_commit,source_digest=source_digest,profile=4,program_sha256=segments[0]['wire_sha256'],binding_sha256=sha(binding[32:]),bit_sha256=profile['bit_sha256'],xsa_sha256=profile['xsa_sha256'],files=records,statuses=dict(OFFLINE='PENDING_FINAL_SUITE',RV32='PASS_STATIC_BUILD',CROSS='NOT_RUN',IMAGE='NOT_RUN',HW4_LINUX='NOT_RUN',H1='NOT_RUN'))
 write_json(out/'release.json',release);return release
def frozen_references(contract):
 """Expected roles/ROI identities from the source-bound v5 contract."""
 out={'v5':{}}
 def record(role,path,roi=None):
  rows=[f for f in contract['files'] if f['role']==role and (roi is None or '/roi%d/'%roi in f['path'])]
  if len(rows)!=1:raise ValueError('FROZEN_REFERENCE_ROLE:'+role)
  return dict(path=path,bytes=rows[0]['bytes'],sha256=rows[0]['sha256'])
 for role,name in [('original_acceptance','acceptance.json'),('original_R2','R2.npy'),('original_ORT_FP32','ORT.npy')]:out[role]=record(role,'references/'+name)
 for roi in [0,26]:out['v5'][str(roi)]=record('logits_payload','references/roi%d.bare.f32'%roi,roi)
 return out

def verify_release(release_dir:Path)->dict:
 out=dict(integrity='FAIL')
 try:
  root=Path(release_dir);r=json.loads(file_bytes(root,'release.json'));contents={}
  if r.get('schema')!=1 or r.get('scope')!='HUNET_OFFLINE_RELEASE':raise ValueError('RELEASE_SCHEMA')
  for name,v in r['files'].items():
   b=file_bytes(root,name)
   if len(b)!=v['bytes'] or sha(b)!=v['sha256']:raise ValueError('SHA256:'+name)
   contents[name]=b
  actual={p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() or p.is_symlink()}
  if actual!=set(contents)|{'release.json'}:raise ValueError('UNLISTED_RELEASE_FILE')
  inv=json.loads(contents['SOURCE_SHA256.json'])
  if inv['source_commit']!=r['source_commit'] or sha(canonical(inv))!=r['source_digest'] or not re.fullmatch('[0-9a-f]{40}',r['source_commit']):raise ValueError('SOURCE_DIGEST')
  source={}
  with tarfile.open(fileobj=io.BytesIO(contents['source.tar.gz']),mode='r:gz') as tar:
   for member in tar:
    name=safe_name(member.name)
    if not member.isfile() or name in source or name not in inv['files']:raise ValueError('SOURCE_ARCHIVE_ALLOWLIST')
    b=tar.extractfile(member).read();v=inv['files'][name]
    if len(b)!=v['bytes'] or sha(b)!=v['sha256'] or hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()!=v['git_blob']:raise ValueError('SOURCE_ARCHIVE_SHA')
    source[name]=b
  if set(source)!=set(inv['files']):raise ValueError('SOURCE_ARCHIVE_MISSING')
  profile=json.loads(source['contracts/profiles/hw4-hunet-v5.json']);frozen=json.loads(source['contracts/hunet_v5.json']);segments=json.loads(source['generated/hunet/segments.json'])
  for key in ['bit_sha256','xsa_sha256']:
   if r[key]!=profile[key]:raise ValueError('HARDWARE_PROFILE_IDENTITY')
  checks={'hardware/top.bit':r['bit_sha256'],'hardware/gpgpu_with_bit.xsa':r['xsa_sha256'],'program6/code.bin':segments[0]['wire_sha256'],'program7/code.bin':segments[1]['wire_sha256']}
  for name,want in checks.items():
   if sha(contents[name])!=want:raise ValueError('CONTROLLED_COMPONENT:'+name)
  for role,name in [('weights','weights.bin'),('jobs_file','jobs.bin'),('aux_file','matrix_descriptors.bin'),('onnx','model.onnx')]:
   if sha(contents[name])!=next(f['sha256'] for f in frozen['files'] if f['role']==role):raise ValueError('FROZEN_COMPONENT:'+name)
  references=frozen_references(frozen)
  if json.loads(contents['references.json'])!=references:raise ValueError('FROZEN_REFERENCE_MAP')
  for v in [references[k] for k in ['original_acceptance','original_R2','original_ORT_FP32']]+list(references['v5'].values()):
   b=contents[v['path']]
   if len(b)!=v['bytes'] or sha(b)!=v['sha256']:raise ValueError('FROZEN_REFERENCE:'+v['path'])
  b=contents['binding.bin'];expected=[sha(source['contracts/profiles/hw4-hunet-v5.json']),segments[0]['wire_sha256'],r['source_digest'],sha(contents['weights.bin']),sha(contents['jobs.bin']),sha(contents['matrix_descriptors.bin']),sha(contents['model.onnx'])]
  if b!=struct.pack('<8I',0x44424c48,1,256,4,6,3,0,0)+b''.join(bytes.fromhex(h) for h in expected) or sha(b[32:])!=r['binding_sha256']:raise ValueError('BINDING_DERIVATION')
  if sha(contents['rv32-dependencies.sha256'])!=inv['rv32_dependencies_sha256'] or len(contents['rv32-dependencies.sha256'].splitlines())<5:raise ValueError('DEPENDENCY_BINDING')
  for record in json.loads(source['contracts/hunet_inputs.json'])['records']:
   name='inputs/%03d.f32'%record['roi_index']
   if record['included'] and sha(contents[name])!=record['sha256']:raise ValueError('INPUT_BINDING')
   if not record['included'] and name in contents:raise ValueError('TRUNCATED_INPUT_INCLUDED')
  out.update(integrity='PASS',source_commit=r['source_commit'],source_digest=r['source_digest'],files=len(contents),profile=4,reference_identity=references)
 except (ValueError,KeyError,TypeError,OSError,tarfile.TarError) as e:out['reason']=str(e)
 return out
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--verify',type=Path);p.add_argument('--source',type=Path,default=ROOT);p.add_argument('--out',type=Path);p.add_argument('--commit');a=p.parse_args()
 result=verify_release(a.verify) if a.verify else package_release(a.source,a.out,a.commit);print(json.dumps(result if a.verify else {k:v for k,v in result.items() if k!='files'},indent=2));raise SystemExit(1 if a.verify and result['integrity']!='PASS' else 0)
