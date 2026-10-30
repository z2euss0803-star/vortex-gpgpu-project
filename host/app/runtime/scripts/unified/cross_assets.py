"""Original static asset byte closure only, never a render/GPU result."""
import pathlib,json,hashlib,re,stat
DECLARATIONS={
 'docs/unified/U16-inputs.json':'95ca51ea0b4c48c93ac9bcc1bdd5edc81a8f262a153d5964a1a3be03f938c16c',
 'docs/unified/U16-oracles.json':'83fbbbd1ade0b552644330d9814fab4a7f113bb20319448e6c1dcd7966d99f55',
 'generated/p8/assets.h':'afa3ca353c4a2aa6954e302e5c594451300709b0d6f0ae83f3c7865f5c46627a'}
def read(root,name,expected_bytes=None):
 relative=pathlib.PurePosixPath(name)
 if relative.is_absolute() or '..' in relative.parts:raise ValueError('asset path scope')
 p=root.joinpath(*relative.parts)
 if p.resolve()!=p:raise ValueError('asset link scope')
 s=p.lstat()
 if not stat.S_ISREG(s.st_mode) or not 0<s.st_size<=32*1024*1024 or (expected_bytes is not None and s.st_size!=expected_bytes):raise ValueError('asset extent/kind')
 raw=p.read_bytes();after=p.lstat()
 if (s.st_ino,s.st_size,s.st_mtime_ns)!=(after.st_ino,after.st_size,after.st_mtime_ns):raise ValueError('asset changed while inspected')
 return raw
def verify_original_assets(root):
 root=pathlib.Path(root).resolve();decl={};asset_files=[];oracle_files=[]
 for name,digest in DECLARATIONS.items():
  raw=read(root,name)
  if hashlib.sha256(raw).hexdigest()!=digest:raise ValueError('frozen declaration digest changed: '+name)
  decl[name]=raw
 spec=json.loads(decl['docs/unified/U16-inputs.json'].decode('utf8'));oracle=json.loads(decl['docs/unified/U16-oracles.json'].decode('utf8'));by_name={}
 for group in spec['sources']:
  for row in group['arrays']:
   name=row['path'].replace('\\','/');parts=pathlib.PurePosixPath(name).parts
   if len(parts)!=3 or parts[:2]!=('data','p8') or parts[2]!=row['symbol']+'.bin' or type(row['words']) is not int or row['words']*4!=row['bytes'] or parts[2] in by_name:raise ValueError('asset frozen array declaration')
   raw=read(root,name,row['bytes']);digest=hashlib.sha256(raw).hexdigest()
   if digest!=row['sha256']:raise ValueError('asset digest changed: '+name)
   item=dict(path=name,bytes=len(raw),sha256=digest,original_c_array=row['symbol']);asset_files.append(item);by_name[parts[2]]=item
 entries=re.findall(r'^\{"([A-Za-z0-9_]+\.bin)",([0-9]+),"([0-9a-f]{64})"\},$',decl['generated/p8/assets.h'].decode('ascii'),re.M)
 if len(entries)!=len(by_name)!=18 or len(entries)!=18 or {x[0] for x in entries}!=set(by_name):raise ValueError('asset runtime bank closure')
 identity=hashlib.sha256()
 for name,words,digest in entries:
  if by_name[name]['bytes']!=int(words)*4 or by_name[name]['sha256']!=digest:raise ValueError('asset runtime header disagreement')
  # Exactly the unchanged C++ P8Assets::load name+digest bank order.
  identity.update(name.encode('ascii'));identity.update(bytes.fromhex(digest))
 for row in oracle['oracles']:
  if row['name'] not in ('projected','color','depth'):raise ValueError('asset oracle set')
  name='data/p8/oracle-'+row['name']+'.bin';raw=read(root,name,row['bytes']);digest=hashlib.sha256(raw).hexdigest()
  if digest!=row['sha256']:raise ValueError('asset oracle digest changed')
  oracle_files.append(dict(path=name,bytes=len(raw),sha256=digest,original_source_offset=row['source_offset']))
 if len(oracle_files)!=3 or {r['path'] for r in oracle_files}!={'data/p8/oracle-'+n+'.bin' for n in ('projected','color','depth')}:raise ValueError('asset oracle closure')
 return dict(asset_files=asset_files,oracle_files=oracle_files,p8_asset_id=identity.hexdigest(),frozen_declarations=DECLARATIONS,original_frame_bundle_sha256=oracle['bundle_sha256'],original_c_reextraction='PREVIOUS_U16_EVIDENCE_NOT_RERUN_HERE',gpu_raw_cycles=None,scope='ORIGINAL_ASSET_BYTE_CLOSURE_ONLY_NOT_RENDER_OR_GPU')
