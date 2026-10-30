"""Pinned host-only Pillow beside this worktree; no global install or board use."""
import hashlib,json,pathlib,stat,sys,zipfile

def dependency():
 spec=json.loads(pathlib.Path('contracts/unified/texture-preprocess-deps.json').read_bytes());wheel=pathlib.Path(spec['remote']);raw=wheel.read_bytes();assert len(raw)==spec['bytes'] and hashlib.sha256(raw).hexdigest()==spec['sha256'];base=pathlib.Path('/home/z2euss/workspace/unified_gpu_runtime_20261021/deps');base.mkdir(exist_ok=True);target=base/('pillow84-'+spec['sha256'][:16]);receipt=target/'pinned-files.json'
 if target.exists():
  proof=json.loads(receipt.read_bytes());assert proof['wheel_sha256']==spec['sha256']
  for row in proof['files']:
   p=target/row['name'];assert p.is_file() and not p.is_symlink() and p.stat().st_size==row['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256']
 else:
  target.mkdir(exist_ok=False);rows=[]
  with zipfile.ZipFile(wheel) as z:
   for entry in z.infolist():
    name=pathlib.PurePosixPath(entry.filename);assert not name.is_absolute() and all(x not in ('.','..') for x in name.parts) and '\\' not in entry.filename and not stat.S_ISLNK(entry.external_attr>>16);p=target/name
    if entry.is_dir():p.mkdir(parents=True,exist_ok=True);continue
    p.parent.mkdir(parents=True,exist_ok=True);data=z.read(entry);assert len(data)==entry.file_size;p.write_bytes(data);rows.append(dict(name=str(name),bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
  proof=dict(wheel_sha256=spec['sha256'],files=rows,scope=spec['purpose']);receipt.write_bytes((json.dumps(proof,indent=2)+'\n').encode())
 sys.path.insert(0,str(target));import PIL;from PIL import Image
 assert PIL.__version__=='8.4.0' and pathlib.Path(PIL.__file__).resolve().parent.parent==target.resolve()
 # Resampling is an enum namespace in newer PC Python. Constant1 still selects
 # the actual Pillow LANCZOS implementation; no filter or image algorithm change.
 alias=not hasattr(Image,'Resampling')
 if alias:
  from types import SimpleNamespace
  Image.Resampling=SimpleNamespace(LANCZOS=Image.LANCZOS)
 assert int(Image.Resampling.LANCZOS)==1
 return dict(version=PIL.__version__,wheel_sha256=spec['sha256'],target=str(target),files=len(proof['files']),resampling_enum_namespace_alias_only=alias,scope='HOST_PREPROCESS_NOT_BOARD')
