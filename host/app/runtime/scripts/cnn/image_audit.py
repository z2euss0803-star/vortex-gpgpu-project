"""Candidate image audit; compilation/rootfs evidence is never board evidence."""
import argparse,gzip,hashlib,json,re,shutil,stat,subprocess,tarfile,zlib
from pathlib import Path
from generate_kernels import generate_program,program_words
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def identity(commit,build):
 profile=json.loads((ROOT/'contracts/profiles/h0-r4.json').read_text())
 return dict(scope='CNN_CANDIDATE_IDENTITY_V1',source_commit=commit,build_id=build,contract='cnn-i32-8x8-v1',
  profile=profile['profile'],bit_sha256=profile['bit_sha256'],xsa_sha256=profile['xsa_sha256'],args_bytes=64,
  programs=[dict(id=i,bytes=4096,instruction_bytes=len(program_words(i))*4,sha256=hashlib.sha256(generate_program(i)).hexdigest()) for i in (2,3,4,5)])
def identity_errors(value,commit,build):
 expected=identity(commit,build);errors=[]
 if not re.fullmatch('[0-9a-f]{40}',commit) or not re.fullmatch('H0-LINUX-CNN-I32-V1-[0-9]{8}T[0-9]{6}Z',build):errors.append('unsealed source/build')
 for k,v in expected.items():
  if value.get(k)!=v:errors.append('identity '+k)
 if type(value.get('args_bytes')) is not int:errors.append('args type')
 for p in value.get('programs',[]):
  if not isinstance(p,dict) or any(type(p.get(k)) is not int for k in ('id','bytes','instruction_bytes')):errors.append('program types')
 return errors
def kernel_errors(config,symbols):
 return [name for name in ('CONFIG_VXL_LINUX=y','CONFIG_ZONE_DMA32=y') if name not in config.splitlines()]+[name for name in ('vxl_init','vxl_engine_poll','vxl_cnn_args_check') if not re.search(r'\b'+name+r'\b',symbols)]
def elf_errors(header,program,is_executable):
 errors=[]
 if not re.search(r'Machine:\s+AArch64\s*(?:\n|$)',header):errors.append('ELF machine')
 if is_executable and '[Requesting program interpreter: /lib/ld-linux-aarch64.so.1]' not in program:errors.append('ELF interpreter')
 return errors
REQUIRED_ROOTFS=('etc/codex-build-id','etc/codex-cnn-manifest.json','usr/bin/vxl_info','usr/bin/vxl_copy_test','usr/bin/vxl_vecadd','usr/bin/vxl_cnn','usr/lib/libvortex.so','usr/lib/libvortex-zynq.so','lib/ld-linux-aarch64.so.1','home/root/.ssh/authorized_keys')+tuple('usr/share/codex-cnn/id%d.bin'%i for i in (2,3,4,5))
def rootfs_missing(files):return sorted(set(REQUIRED_ROOTFS)-set(files))

def normalized_name(name):
 while name.startswith('./'):name=name[2:]
 if name=='.':name=''
 if name.startswith('/') or '..' in name.split('/') or '\0' in name:raise ValueError('Unsafe archive path')
 return name.rstrip('/')

def cpio_entries(raw):
 entries={};groups={};offset=0;trailer=False
 while offset+110<=len(raw):
  header=raw[offset:offset+110];offset+=110
  if header[:6] not in (b'070701',b'070702'):raise ValueError('Require newc/crc cpio')
  fields=[int(header[6+i*8:14+i*8],16) for i in range(13)]
  inode,mode,uid,gid,nlink,mtime,size,major,minor,rmajor,rminor,namesize,check=fields
  if not namesize or offset+namesize>len(raw):raise ValueError('Truncated cpio name')
  encoded=raw[offset:offset+namesize]
  if encoded[-1:]!=b'\0' or b'\0' in encoded[:-1]:raise ValueError('Invalid cpio name')
  name=normalized_name(encoded[:-1].decode('utf-8'));offset=(offset+namesize+3)&~3
  if offset+size>len(raw):raise ValueError('Truncated cpio data')
  data=raw[offset:offset+size];offset=(offset+size+3)&~3
  if header[:6]==b'070702' and sum(data)&0xffffffff!=check:raise ValueError('cpio CRC')
  if name=='TRAILER!!!':
   if size or any(raw[offset:]):raise ValueError('Unexpected trailing cpio data')
   trailer=True;break
  if name in entries:raise ValueError('Duplicate cpio path')
  kind=stat.S_IFMT(mode)
  if kind not in (stat.S_IFREG,stat.S_IFDIR,stat.S_IFLNK,stat.S_IFCHR,stat.S_IFBLK,stat.S_IFIFO):raise ValueError('Unsupported cpio file type')
  entries[name]=dict(kind=kind,mode=mode&0o7777,uid=uid,gid=gid,data=data,device=(rmajor,rminor) if kind in (stat.S_IFCHR,stat.S_IFBLK) else None)
  if kind==stat.S_IFREG and nlink>1:groups.setdefault((major,minor,inode),[]).append(name)
 if not trailer:raise ValueError('Missing cpio trailer')
 for names in groups.values():
  payloads={entries[name]['data'] for name in names if entries[name]['data']}
  if len(payloads)>1:raise ValueError('Conflicting cpio hardlink data')
  data=next(iter(payloads),b'')
  for name in names:entries[name]['data']=data
 return entries

CPIO_INIT_RECIPE_SHA256="c35850c69abf3c74f0dfcc44f3ce66e967a1a3dcec7a334821d72bf9a22e0635"

def link_bytes(data):
 while data.startswith(b'./'):data=data[2:]
 return data

def rootfs_payload_errors(raw,tarpath,cpio_init_recipe_sha256=None):
 try:
  boot=cpio_entries(raw);archive={}
  with tarfile.open(tarpath) as tar:
   for member in tar.getmembers():
    name=normalized_name(member.name)
    if name in archive:raise ValueError('Duplicate tar path')
    kind=stat.S_IFREG if member.isfile() or member.islnk() else stat.S_IFDIR if member.isdir() else stat.S_IFLNK if member.issym() else stat.S_IFCHR if member.ischr() else stat.S_IFBLK if member.isblk() else stat.S_IFIFO if member.isfifo() else 0
    if not kind:raise ValueError('Unsupported tar file type')
    data=tar.extractfile(member).read() if kind==stat.S_IFREG else member.linkname.encode('utf-8') if kind==stat.S_IFLNK else b''
    archive[name]=dict(kind=kind,mode=member.mode&0o7777,uid=member.uid,gid=member.gid,data=data,device=(member.devmajor,member.devminor) if kind in (stat.S_IFCHR,stat.S_IFBLK) else None)
  # The verified Yocto image_types.bbclass appends exactly this init symlink.
  # It is not present in tar, and is accepted only with the exact recipe hash.
  if cpio_init_recipe_sha256==CPIO_INIT_RECIPE_SHA256 and 'init' not in archive and 'sbin/init' in archive:
   archive['init']=dict(kind=stat.S_IFLNK,mode=0o777,uid=0,gid=0,data=b'/sbin/init',device=None)
  # Strip only a redundant leading ./ from link targets; preserve absolute,
  # relative and .. semantics instead of resolving arbitrary symlink paths.
  for table in (boot,archive):
   for value in table.values():
    if value['kind']==stat.S_IFLNK:value['data']=link_bytes(value['data'])
  errors=['boot cpio/tar path inventory'] if set(boot)!=set(archive) else []
  errors+=['boot cpio/tar entry '+name for name in sorted(set(boot)&set(archive)) if boot[name]!=archive[name]]
  return errors
 except (OSError,ValueError,KeyError,tarfile.TarError) as e:return ['boot cpio/tar malformed: '+str(e)]
def source_errors(snapshot,actual,inventory):
 errors=[];hashes={}
 for name in sorted(inventory):
  relative=Path(name)
  if relative.parts[0] not in ('driver','core','include','generated'):continue
  path=snapshot/relative;dest=actual/relative
  if not dest.is_file():errors.append('missing compiled source '+name);continue
  if name=='driver/Makefile':
   expected=path.read_text().replace('obj-m += vxl_linux.o','obj-$(CONFIG_VXL_LINUX) += vxl_linux.o')
   expected=re.sub(r'^ccflags-y \+= -I.* -Werror','ccflags-y += -I$(srctree)/drivers/misc/vxl/include -Werror',expected,flags=re.M)
   if dest.read_text()!=expected:errors.append('kernel Makefile integration')
  elif dest.read_bytes()!=path.read_bytes():errors.append('compiled source '+name)
  hashes[name]=sha(dest)
 return errors,hashes
def audit(base,snapshot,run_id):
 p=base/'env_backend';images=p/'images/linux';out=base/run_id;out.mkdir(exist_ok=False)
 integration=json.loads((base/'integration.json').read_text());commit=integration['source_commit'];build=integration['build_id']
 if (base/'build.exit').read_text().strip()!='0':raise ValueError('Candidate build exit')
 if sha(Path(integration['boot_bin']))!=integration['boot_bin_sha256']:raise ValueError('Protected firmware identity')
 if (snapshot/'SOURCE_COMMIT').read_text().strip()!=commit:raise ValueError('Snapshot commit')
 source_inventory=json.loads((snapshot/'SOURCE_SHA256.json').read_text())
 for name,digest in source_inventory.items():
  if sha(snapshot/name)!=digest:raise ValueError('Source snapshot changed '+name)
 for name,digest in json.loads((base/'protected-original.json').read_text()).items():
  if sha(Path(name))!=digest:raise ValueError('Protected original changed '+name)
 k=next((p/'build/tmp/work/zynqmp_generic-xilinx-linux/linux-xlnx').glob('*-r0'))
 cfg=next(k.glob('linux-*-build/.config'));symbols=next(k.glob('linux-*-build/System.map'))
 errors=kernel_errors(cfg.read_text(),symbols.read_text())
 actual=p/'build/tmp/work-shared/zynqmp-generic/kernel-source/drivers/misc/vxl'
 source_failures,kernel_sources=source_errors(snapshot,actual,source_inventory);errors+=source_failures
 native=k/'recipe-sysroot-native/usr/bin';dt=str(images/'system.dtb');node='/amba_pl@0/vortex-backend@a0000000'
 def x(n,prop):return subprocess.check_output([str(native/'fdtget'),'-t','x',dt,n,prop]).decode().split()
 def s(n,prop):return subprocess.check_output([str(native/'fdtget'),'-t','s',dt,n,prop]).decode().strip()
 if s(node,'compatible')!='vortex,h0-r4-v1' or x(node,'reg')!=['0','a0000000','0','10000','0','b0010000','0','10000']:errors.append('DT resources')
 if x('/reserved-memory/vortex-ps@10000000','reg')!=['0','10000000','0','100000']:errors.append('PS pool')
 if 'vortex,ps-placement-approved' not in subprocess.check_output([str(native/'fdtget'),'-p',dt,node]).decode().split():errors.append('placement approval')
 if s('/amba_pl@0/dma@b0010000','status')!='disabled':errors.append('CDMA ownership')
 if x(node,'clocks')[1]!='47' or x(node,'resets')[1]!='74':errors.append('clock/reset')
 if x('/amba_pl@0/gpio@b0020000','clocks')[1]!='47' or 'clk_ignore_unused' in s('/chosen','bootargs'):errors.append('DT GPIO/bootargs')
 dram=[int(v,16) for v in x('/memory','reg')]
 ranges=[((dram[z]<<32)|dram[z+1],(dram[z+2]<<32)|dram[z+3]) for z in range(0,len(dram),4)]
 if not all(any(start<=lo and hi<=start+size for start,size in ranges) for lo,hi in ((0x10000000,0x10100000),(0x20000000,0x40000000))):errors.append('RAM placement')
 for index,name in enumerate(('fit-kernel.gz','fit-system.dtb','fit-rootfs.bin')):
  subprocess.check_call([str(native/'dumpimage'),'-T','flat_dt','-p',str(index),'-o',str(out/name),str(images/'image.ub')],stdout=subprocess.DEVNULL)
 if gzip.decompress((out/'fit-kernel.gz').read_bytes())!=(images/'Image').read_bytes() or (out/'fit-system.dtb').read_bytes()!=(images/'system.dtb').read_bytes() or (out/'fit-rootfs.bin').read_bytes()!=(images/'rootfs.cpio.gz').read_bytes():errors.append('FIT actual bytes')
 recipe=p/'components/yocto/layers/core/meta/classes/image_types.bbclass'
 recipe_sha=sha(recipe)
 if recipe_sha!=CPIO_INIT_RECIPE_SHA256:errors.append('unrecognized cpio init recipe')
 shutil.copy2(str(recipe),str(out/'image_types.bbclass'))
 errors+=rootfs_payload_errors(gzip.decompress((out/'fit-rootfs.bin').read_bytes()),images/'rootfs.tar.gz',recipe_sha)
 if 0x80000+(images/'Image').stat().st_size>=0x10000000 or 0x20000000+(images/'image.ub').stat().st_size>=0x3fffffff:errors.append('FIT/kernel pool collision')
 tool_hashes={}
 with tarfile.open(images/'rootfs.tar.gz') as tar:
  files={m.name.lstrip('./').strip('/'):m for m in tar.getmembers()}
  missing=rootfs_missing(files)
  if missing:raise ValueError('Missing installed rootfs payload: '+', '.join(missing))
  def data(name):return tar.extractfile(files[name]).read()
  actual_build=data('etc/codex-build-id').decode().strip()
  errors+=identity_errors(json.loads(data('etc/codex-cnn-manifest.json').decode()),commit,build)
  if actual_build!=build:errors.append('installed build-id')
  for name in ('usr/bin/vxl_info','usr/bin/vxl_copy_test','usr/bin/vxl_vecadd','usr/bin/vxl_cnn','usr/lib/libvortex.so','usr/lib/libvortex-zynq.so'):
   content=data(name);target=out/Path(name).name;target.write_bytes(content)
   header=subprocess.check_output(['readelf','-h',str(target)]).decode();program=subprocess.check_output(['readelf','-l',str(target)]).decode()
   errors+=elf_errors(header,program,name.startswith('usr/bin'))
   tool_hashes[name]=sha(target)
  if commit.encode() not in data('usr/bin/vxl_cnn'):errors.append('tool compiled commit')
  if 'lib/ld-linux-aarch64.so.1' not in files or not any(n.startswith('usr/lib/libstdc++.so.') for n in files):errors.append('rootfs runtime dependencies')
  for id in (2,3,4,5):
   if data('usr/share/codex-cnn/id%d.bin'%id)!=generate_program(id):errors.append('installed frozen program')
  old=Path(integration['source_project'])/'project-spec/meta-user/recipes-apps/codex-firstboot/files/authorized_keys'
  if data('home/root/.ssh/authorized_keys')!=old.read_bytes() or files['home/root/.ssh'].mode&0o777!=0o700 or files['home/root/.ssh/authorized_keys'].mode&0o777!=0o600:errors.append('inherited key/modes')
 script=(images/'boot.scr').read_bytes();header=bytearray(script[:64]);crc=int.from_bytes(header[4:8],'big');header[4:8]=b'\0'*4
 payload=script[72:].decode()
 if zlib.crc32(header)!=crc or zlib.crc32(script[64:])!=int.from_bytes(script[24:28],'big') or '\r' in payload or 'root=/dev/ram0 rw' not in payload or 'fatload mmc 1:1 0x20000000 image.ub' not in payload or 'setenv fdt_high 0x0fffffff' not in payload or 'setenv initrd_high 0x3fffffff' not in payload or not payload.endswith('exit 1\n'):errors.append('SD boot script')
 result=dict(scope='CNN_IMAGE_OFFLINE_AUDIT',source_commit=commit,build_id=build,pass_=not errors,errors=errors,kernel_sources=kernel_sources,tools=tool_hashes,rootfs_binding='ALL_ENTRIES_WITH_VERIFIED_YOCTO_INIT_AND_LEADING_DOT_LINK_NORMALIZATION',cpio_init_recipe_sha256=recipe_sha,
  files={name:dict(bytes=(images/name).stat().st_size,sha256=sha(images/name)) for name in ('Image','system.dtb','rootfs.cpio.gz','rootfs.tar.gz','image.ub','boot.scr')},
  bit_sha256=identity(commit,build)['bit_sha256'],xsa_sha256=identity(commit,build)['xsa_sha256'],rtl='SEPARATE_RECEIPT_REQUIRED',board='NOT_RUN',sd='NOT_TOUCHED',h1='NOT_RUN')
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 if errors:raise ValueError('; '.join(errors))
 release=out/'release';release.mkdir()
 shutil.copy2(str(Path(integration['boot_bin'])),str(release/'BOOT.BIN'))
 for name in ('boot.scr','image.ub','system.dtb'):shutil.copy2(str(images/name),str(release/name))
 result['release']={name:dict(bytes=(release/name).stat().st_size,sha256=sha(release/name)) for name in ('BOOT.BIN','boot.scr','image.ub','system.dtb')}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 print('CNN_BUILD_AUDIT_PASS_RTL_SEPARATE_NOT_BOARD',flush=True)
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('--base',required=True);a.add_argument('--snapshot',required=True);a.add_argument('--run-id',required=True);o=a.parse_args();audit(Path(o.base),Path(o.snapshot),o.run_id)
