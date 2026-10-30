"""Exact cache proof/retirement, not a directory selector or remote RPC API.

The existing coordinated transfer creates the archive at an independent
destination. archive_verified reads every member using the original NNRT
verifier. A trusted internal _guard must recheck activity/dependencies/locks;
JSON assertions alone cannot authorize deletion. A locally readable archive
is required for live release; remote-only receipts fail closed. Existing
authenticated A/B stream/release controllers remain separate callers.
"""
import pathlib,os,json,stat,hashlib,shutil
from scripts.nnrt.archive_manifest import verify_archive,safe_name

class ReleaseStopped(ValueError):
 def __init__(self,message,removed):
  super().__init__(message);self.receipt=dict(released=False,removed_members=list(removed),archive='KEEP',gpu_raw_cycles=None)

def portable(manifest):return {k:v for k,v in manifest.items() if not k.startswith('_')}
def sha(path):
 h=hashlib.sha256()
 with pathlib.Path(path).open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def admissible(m):
 if not isinstance(m,dict) or m.get('cache_kind')!='exclusive-rebuildable' or m.get('approved_exact_root')!=m.get('source_root'):raise ValueError('exact exclusive approved cache required')
 raw=m['source_root'];parts=pathlib.PurePosixPath(raw.replace('\\','/')).parts
 if len(parts)<3 or any(p.lower() in ('downloads','images','project-spec','components','kernel-source','kernel-build','sdk','sysroot') or 'testdisk' in p.lower() or 'boot-working' in p.lower() for p in parts):raise ValueError('protected source or recovery path')
 guard=m.get('_guard')
 if not callable(guard):raise ValueError('fresh internal ownership guard required, never peer assertions')
 facts=guard()
 if not isinstance(facts,dict) or facts.get('source_root')!=raw or any(facts.get(k) is not True for k in ('authorized','exclusive','rebuildable','inactive','dependencies_clear','locks_owned')):raise ValueError('ownership/activity/dependency/coordination gate refused')
 return facts
def archive_verified(cache_manifest,destination):
 admissible(cache_manifest)
 proof=verify_archive(destination,portable(cache_manifest))
 proof.update(scope='EXACT_CACHE_EVERY_MEMBER_PROOF_NOT_GPU',gpu_raw_cycles=None)
 return proof
def reparse(path):
 s=path.lstat();return stat.S_ISLNK(s.st_mode) or bool(getattr(s,'st_file_attributes',0)&getattr(stat,'FILE_ATTRIBUTE_REPARSE_POINT',0x400))
def scan(root):
 if not root.is_absolute() or not root.is_dir() or reparse(root) or root.resolve()!=root:raise ValueError('exact real cache root required')
 uid=os.geteuid() if hasattr(os,'geteuid') else root.stat().st_uid
 files=[];directories=[]
 for folder,dirs,names in os.walk(str(root),followlinks=False):
  for name in sorted(dirs+names):
   p=pathlib.Path(folder)/name;s=p.lstat()
   if reparse(p) or s.st_uid!=uid:raise ValueError('cache link/reparse or foreign owner')
   rel=p.relative_to(root).as_posix();safe_name(rel)
   if stat.S_ISDIR(s.st_mode):directories.append(rel)
   elif stat.S_ISREG(s.st_mode):
    if s.st_nlink!=1:raise ValueError('shared cache hardlink refused')
    files.append(dict(path=rel,bytes=s.st_size,sha256=sha(p),inode=s.st_ino,device=s.st_dev,mtime_ns=s.st_mtime_ns,mode=stat.S_IMODE(s.st_mode),uid=s.st_uid,links=s.st_nlink))
   else:raise ValueError('special cache member refused')
 return sorted(files,key=lambda r:r['path']),sorted(directories)
def parent_fd(root_fd,parts):
 fd=os.dup(root_fd)
 try:
  for part in parts:
   new=os.open(part,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW,dir_fd=fd);os.close(fd);fd=new
  return fd
 except BaseException:os.close(fd);raise
def same_stat(s,row):return (s.st_ino,s.st_dev,s.st_mtime_ns,s.st_size,stat.S_IMODE(s.st_mode),s.st_uid,s.st_nlink)==(row['inode'],row['device'],row['mtime_ns'],row['bytes'],row['mode'],row['uid'],row['links'])
def release_verified(manifest,archive_receipt):
 removed=[];root_fd=None
 try:
  admissible(manifest);expected=portable(manifest)
  if not isinstance(archive_receipt,dict) or archive_receipt.get('verified') is not True or archive_receipt.get('manifest')!=expected:raise ValueError('exact archive receipt required')
  proof=archive_verified(manifest,archive_receipt['archive'])
  if proof['archive_sha256']!=archive_receipt.get('archive_sha256') or proof['member_count']!=archive_receipt.get('member_count'):raise ValueError('archive changed since verification')
  root=pathlib.Path(manifest['source_root']);before=shutil.disk_usage(str(root)).free
  files,dirs=scan(root)
  if files!=sorted(manifest['files'],key=lambda r:r['path']) or dirs!=sorted(manifest['directories']):raise ValueError('source changed or new write before release')
  admissible(manifest)
  posix=os.name=='posix' and os.open in os.supports_dir_fd and os.unlink in os.supports_dir_fd
  if posix:root_fd=os.open(str(root),os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
  for row in files:
   admissible(manifest);parts=safe_name(row['path']).parts
   if posix:
    parent=parent_fd(root_fd,parts[:-1])
    try:
     current=os.stat(parts[-1],dir_fd=parent,follow_symlinks=False)
     if not same_stat(current,row):raise ValueError('member changed immediately before unlink')
     os.unlink(parts[-1],dir_fd=parent)
    finally:os.close(parent)
   else:
    p=root/row['path']
    if os.path.commonpath([str(p.resolve()),str(root)])!=str(root) or reparse(p) or not same_stat(p.lstat(),row):raise ValueError('resolved member changed or escaped exact source')
    os.unlink(str(p))
   removed.append(row['path'])
  for rel in sorted(dirs,key=lambda s:(s.count('/'),s),reverse=True):
   admissible(manifest);parts=safe_name(rel).parts
   if posix:
    parent=parent_fd(root_fd,parts[:-1])
    try:os.rmdir(parts[-1],dir_fd=parent)
    finally:os.close(parent)
   else:
    p=root/rel
    if os.path.commonpath([str(p.resolve()),str(root)])!=str(root) or reparse(p):raise ValueError('directory changed or escaped exact source')
    p.rmdir()
  if list(root.iterdir()):raise ValueError('new write observed after incremental release')
  return dict(released=True,removed_members=removed,source_root=str(root),source_root_retained=True,archive=proof['archive'],before_free_bytes=before,after_free_bytes=shutil.disk_usage(str(root)).free,gpu_raw_cycles=None)
 except (ValueError,OSError,KeyError,TypeError) as error:raise ReleaseStopped(str(error),removed)
 finally:
  if root_fd is not None:os.close(root_fd)
