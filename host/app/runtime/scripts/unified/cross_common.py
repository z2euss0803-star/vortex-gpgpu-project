"""Shared cross-build evidence utilities; no device access."""
import pathlib,hashlib,json,os,subprocess,time,re,stat

def sha(path):
 h=hashlib.sha256()
 with pathlib.Path(path).open('rb') as f:
  for part in iter(lambda:f.read(1048576),b''):h.update(part)
 return h.hexdigest()

def snapshot(root):
 root=pathlib.Path(root).resolve();rows=[];excluded={'build','evidence','.git','.superpowers','__pycache__','.pytest_cache'}
 for folder,dirs,files in os.walk(str(root),followlinks=False):
  dirs[:]=sorted(d for d in dirs if d not in excluded)
  for d in dirs:
   if pathlib.Path(folder,d).is_symlink():raise ValueError('source directory symlink')
  for name in sorted(files):
   if name=='.git':continue
   path=pathlib.Path(folder,name)
   if path.is_symlink() or not path.is_file():raise ValueError('regular source files required')
   rows.append(dict(path=path.relative_to(root).as_posix(),bytes=path.stat().st_size,sha256=sha(path)))
 rows.sort(key=lambda r:r['path']);runtime=[r for r in rows if r['path'].startswith(('unified/','nnrt/','include/','core/','driver/','userspace/','tools/','lib/','generated/','contracts/','image/','p8/','adapters/','vendor/'))]
 def identity(domain,values):return hashlib.sha256(domain.encode()+b'\0'+json.dumps(values,sort_keys=True,separators=(',',':')).encode()).hexdigest()
 return dict(source_snapshot_id=identity('VXU22_SOURCE_SNAPSHOT_V1',rows),runtime_source_id=identity('VXU22_RUNTIME_INPUTS_V1',runtime),files=rows,runtime_files=runtime,kernel_source_id=None)

def validate_provider_identity(row):
 flag=row.get('provider_flags')
 compatible={1:'vortex,hw4-s8-demo-v1',2:'vortex,w4-graphics-p8-v1',4:'vortex,hw4-s8-joint-v1'}
 if type(flag) is not int or flag not in compatible or row.get('compatible')!=compatible[flag]:raise ValueError('explicit versioned provider required')
 if flag==4 and any(row.get(k)!=v for k,v in dict(hardware_contract='1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d',bit_sha256='94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1',xsa_sha256='02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94').items()):raise ValueError('joint provider must bind the exact frozen S8 hardware')
 return row

def aarch64_target(flag):
 if type(flag) is not int or flag not in (1,2,4):raise ValueError('explicit provider flag required')
 return {1:'aarch64-s8',2:'aarch64-p8',4:'aarch64-s8-joint'}[flag]

def provider(root,flag):
 if type(flag) is not int or flag not in (1,2,4):raise ValueError('explicit provider flag required')
 rows=json.loads((pathlib.Path(root)/'contracts/unified/providers.json').read_text())['providers'];row=next(r for r in rows if r['provider_flags']==flag)
 if row['contexts']!=1 or row['preempt'] is not False or row['board_identity_verified'] is not False or row['namespaces']!={1:[1,3],2:[2],4:[1,2,3]}[flag]:raise ValueError('frozen provider contract')
 for key in ('hardware_contract','bit_sha256','xsa_sha256'):
  if not re.fullmatch('[0-9a-f]{64}',row[key]):raise ValueError('full hardware digest required')
 return validate_provider_identity(row)

def verify_source_lock(root,path,expected_sha256):
 path=pathlib.Path(path);state=path.lstat()
 if not stat.S_ISREG(state.st_mode) or not 0<state.st_size<=16*1024*1024 or not isinstance(expected_sha256,str) or not re.fullmatch('[0-9a-f]{64}',expected_sha256):raise ValueError('bounded regular frozen source lock/digest required')
 raw=path.read_bytes()
 if hashlib.sha256(raw).hexdigest()!=expected_sha256:raise ValueError('frozen source lock digest mismatch')
 def unique(pairs):
  result={}
  for key,value in pairs:
   if key in result:raise ValueError('duplicate source lock key')
   result[key]=value
  return result
 document=json.loads(raw.decode('utf8'),object_pairs_hook=unique)
 if set(document)!={'schema','snapshot'} or type(document['schema']) is not int or document['schema']!=1:raise ValueError('source lock schema mismatch')
 observed=snapshot(root)
 # Python considers True == 1; frozen JSON identities must also match types.
 canonical=lambda value:json.dumps(value,sort_keys=True,separators=(',',':'))
 if canonical(document['snapshot'])!=canonical(observed):raise ValueError('source differs from approved frozen lock before cross build')
 after=path.lstat()
 if (after.st_ino,after.st_size,after.st_mtime_ns)!=(state.st_ino,state.st_size,state.st_mtime_ns):raise ValueError('source lock changed while inspected')
 return observed

def new_build_output(root,out):
 root=pathlib.Path(root).resolve();base=(root/'build').resolve();out=pathlib.Path(out).resolve()
 if base!=root/'build':raise ValueError('source/build must be a direct real workspace directory')
 try:relative=out.relative_to(base)
 except ValueError:raise ValueError('cross outputs must stay under the explicit source/build workspace')
 if not relative.parts:raise ValueError('a new named cross build directory is required')
 out.mkdir(parents=True,exist_ok=False);return out

class CommandLog:
 def __init__(self,root,out,record):self.root=pathlib.Path(root);self.out=pathlib.Path(out);self.record=record
 def run(self,argv,label):
  if not re.fullmatch('[A-Za-z0-9_-]+',label):raise ValueError('bounded log label required')
  argv=list(map(str,argv));path=self.out/(label+'.log');start=time.monotonic()
  with path.open('xb') as f:p=subprocess.run(argv,cwd=str(self.root),stdout=f,stderr=subprocess.STDOUT)
  text=path.read_text(errors='replace');self.record['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-start,log=str(path),sha256=sha(path)));(self.out/'receipt.json').write_text(json.dumps(self.record,indent=2)+'\n');print(text[-2000:],flush=True)
  if p.returncode:raise ValueError(label+' natural_exit='+str(p.returncode)+'; original log retained')
  return text
