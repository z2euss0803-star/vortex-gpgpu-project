import gzip,io,stat,sys,tarfile,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/cnn'))
def cpio(entries):
 out=bytearray()
 for inode,(name,mode,data) in enumerate(entries+[('TRAILER!!!',0,b'')],1):
  encoded=name.encode()+b'\0';fields=(inode,mode,0,0,1,0,len(data),0,0,0,0,len(encoded),0)
  out+=b'070701'+b''.join(('%08x'%v).encode() for v in fields)+encoded
  out+=b'\0'*((-len(out))%4);out+=data;out+=b'\0'*((-len(out))%4)
 return bytes(out)
class RootfsBinding(unittest.TestCase):
 def test_verified_yocto_init_and_safe_dot_symlinks(self):
  import image_audit as m
  entries=[('.',stat.S_IFDIR|0o755,b''),('sbin/init',stat.S_IFLNK|0o777,b'/bin/busybox'),('usr/sbin/dropbear',stat.S_IFLNK|0o777,b'./dropbearmulti')]
  recipe='c35850c69abf3c74f0dfcc44f3ce66e967a1a3dcec7a334821d72bf9a22e0635'
  with tempfile.TemporaryDirectory() as d:
   path=Path(d)/'rootfs.tar.gz'
   with tarfile.open(path,'w:gz') as tar:
    for name,mode,data in entries:
     t=tarfile.TarInfo(name);t.mode=mode&0o7777;t.uid=t.gid=0
     if stat.S_ISDIR(mode):t.type=tarfile.DIRTYPE
     else:t.type=tarfile.SYMTYPE;t.linkname=data.decode()
     tar.addfile(t)
   boot=list(entries);boot[2]=(boot[2][0],boot[2][1],b'dropbearmulti');boot.append(('init',stat.S_IFLNK|0o777,b'/sbin/init'))
   self.assertEqual(m.rootfs_payload_errors(cpio(boot),path,recipe),[])
   self.assertTrue(m.rootfs_payload_errors(cpio(boot),path,'0'*64))
   for name,mode,data in [('init',stat.S_IFLNK|0o777,b'/evil/init'),('init',stat.S_IFLNK|0o755,b'/sbin/init'),('unexpected',stat.S_IFREG|0o644,b'extra')]:
    bad=boot[:-1]+[(name,mode,data)]
    self.assertTrue(m.rootfs_payload_errors(cpio(bad),path,recipe))
 def test_boot_cpio_tar_binding(self):
  import image_audit as m
  self.assertTrue(hasattr(m,'rootfs_payload_errors'),'Boot cpio鈫攖ar content binding missing')
  entries=[('.',stat.S_IFDIR|0o755,b''),('etc',stat.S_IFDIR|0o755,b''),('etc/codex-cnn-manifest.json',stat.S_IFREG|0o644,b'new-identity'),('usr/bin/vxl_cnn',stat.S_IFREG|0o755,b'new-CNN'),('lib/loader',stat.S_IFLNK|0o777,b'/lib/real-loader')]
  with tempfile.TemporaryDirectory() as d:
   tarpath=Path(d)/'rootfs.tar.gz'
   with tarfile.open(tarpath,'w:gz') as tar:
    for name,mode,data in entries:
     t=tarfile.TarInfo(name);t.mode=mode&0o7777;t.uid=t.gid=0
     if stat.S_ISDIR(mode):t.type=tarfile.DIRTYPE
     elif stat.S_ISLNK(mode):t.type=tarfile.SYMTYPE;t.linkname=data.decode()
     else:t.size=len(data)
     tar.addfile(t,io.BytesIO(data) if t.isfile() else None)
   self.assertEqual(m.rootfs_payload_errors(cpio(entries),tarpath),[])
   for change in ('missing_tool','old_manifest','mode','link','extra','truncated'):
    bad=list(entries)
    if change=='missing_tool':bad.pop(3)
    if change=='old_manifest':bad[2]=(bad[2][0],bad[2][1],b'old-identity')
    if change=='mode':bad[3]=(bad[3][0],stat.S_IFREG|0o777,bad[3][2])
    if change=='link':bad[4]=(bad[4][0],bad[4][1],b'/evil-loader')
    if change=='extra':bad.append(('unexpected',stat.S_IFREG|0o644,b'extra'))
    raw=cpio(bad);raw=raw[:-100] if change=='truncated' else raw
    self.assertTrue(m.rootfs_payload_errors(raw,tarpath),change)
if __name__=='__main__':unittest.main()
