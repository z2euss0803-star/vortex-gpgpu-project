"""Copy a completed HOST test's original private files without following links."""
import pathlib,os,re,stat,shutil
from scripts.unified.check_u11 import sha

def capture(text,out,command):
 rows=[]
 pattern=r'^NATIVE_(?:DAEMON|FILES)_OWNER=(/tmp/vx-native-(?:daemon|p8-daemon|installed-image|files)-[A-Za-z0-9]{6})$'
 for index,owner in enumerate(re.findall(pattern,text,re.M)):
  source=pathlib.Path(owner);s=source.lstat()
  assert stat.S_ISDIR(s.st_mode) and s.st_uid==os.geteuid() and stat.S_IMODE(s.st_mode)==0o700
  dest=out/('private-%03d-%02d'%(command,index));dest.mkdir(mode=0o700);members=[]
  for folder,dirs,files in os.walk(str(source),followlinks=False):
   for name in sorted(dirs+files):
    path=pathlib.Path(folder)/name;s=path.lstat();assert s.st_uid==os.geteuid()
    target=dest/path.relative_to(source);row=dict(path=str(path.relative_to(source)),mode=stat.S_IMODE(s.st_mode),original_links=s.st_nlink)
    if stat.S_ISDIR(s.st_mode):target.mkdir(mode=stat.S_IMODE(s.st_mode));row['kind']='directory'
    elif stat.S_ISREG(s.st_mode):
     shutil.copyfile(str(path),str(target));assert sha(path)==sha(target)
     after=path.lstat();assert (s.st_ino,s.st_size,s.st_mtime_ns)==(after.st_ino,after.st_size,after.st_mtime_ns)
     target.chmod(stat.S_IMODE(s.st_mode));row.update(kind='file',bytes=s.st_size,sha256=sha(target))
    elif stat.S_ISLNK(s.st_mode):row.update(kind='symlink_metadata_only_no_follow',target=os.readlink(str(path)))
    else:row['kind']='nonfile_metadata_only'
    members.append(row)
  rows.append(dict(owner=owner,copy=str(dest),members=members))
 return rows
