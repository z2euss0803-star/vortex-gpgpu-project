"""U23 preparation: hardware metadata is not a built-kernel or board result."""
import hashlib,json,pathlib,re,stat,struct,zipfile,os
import xml.etree.ElementTree as ET

def read_fdt(raw,max_bytes=16*1024**2):
 """Read bounded flattened-tree bytes; never trust an accompanying DTS dump."""
 if type(max_bytes) is not int or not 56<=max_bytes<=512*1024**2 or not isinstance(raw,bytes) or not 56<=len(raw)<=max_bytes:raise ValueError('bounded FDT bytes required')
 magic,total,so,strings_off,reserve,version,compatible,bootcpu,strings_size,structure_size=struct.unpack_from('>10I',raw)
 if magic!=0xd00dfeed or total!=len(raw) or version!=17 or compatible>17 or so%4 or reserve%8:raise ValueError('complete version17 FDT header required')
 spans=[(so,so+structure_size),(strings_off,strings_off+strings_size)]
 if any(start<40 or end<start or end>total for start,end in spans) or max(spans[0][0],spans[1][0])<min(spans[0][1],spans[1][1]):raise ValueError('overlapping or out-of-range FDT blocks')
 limit=min(so,strings_off)
 if reserve<40 or reserve>=limit:raise ValueError('FDT reservation map outside reserved header area')
 pos=reserve
 while True:
  if pos+16>limit:raise ValueError('unterminated FDT reservation map')
  address,length=struct.unpack_from('>QQ',raw,pos);pos+=16
  if address==length==0:break
  if not length or address+length>1<<64:raise ValueError('invalid FDT reservation extent')
 names=raw[strings_off:strings_off+strings_size];end=so+structure_size;pos=so;stack=[];nodes={};closed=False;properties=0
 def word():
  nonlocal pos
  if pos+4>end:raise ValueError('truncated FDT token')
  value=struct.unpack_from('>I',raw,pos)[0];pos+=4;return value
 def text(data):
  try:value=data.decode('ascii')
  except UnicodeDecodeError:raise ValueError('non-ASCII FDT name')
  if '/' in value or '\x00' in value:raise ValueError('invalid FDT name')
  return value
 while pos<end:
  token=word()
  if token==1:
   stop=raw.find(b'\0',pos,end)
   if stop<0 or stop-pos>255 or len(stack)>=64 or len(nodes)>=4096:raise ValueError('unbounded FDT node')
   name=text(raw[pos:stop]);pos=(stop+4)&~3
   if pos>end or (not stack and (name or nodes)) or (stack and not name):raise ValueError('invalid FDT root or node name')
   path='/' if not stack else stack[-1].rstrip('/')+'/'+name
   if path in nodes:raise ValueError('duplicate FDT node')
   nodes[path]={};stack.append(path)
  elif token==2:
   if not stack:raise ValueError('unbalanced FDT END_NODE')
   stack.pop()
  elif token==3:
   size=word();offset=word();properties+=1
   if not stack or properties>16384 or size>end-pos or offset>=len(names):raise ValueError('invalid FDT property range')
   stop=names.find(b'\0',offset)
   if stop<0 or stop-offset>255:raise ValueError('unterminated FDT property name')
   name=text(names[offset:stop]);node=nodes[stack[-1]]
   if not name or name in node:raise ValueError('empty or duplicate FDT property')
   node[name]=raw[pos:pos+size];pos=(pos+size+3)&~3
   if pos>end:raise ValueError('FDT property padding outside structure')
  elif token==4:continue
  elif token==9:
   if stack or '/' not in nodes or any(raw[pos:end]):raise ValueError('unbalanced or trailing FDT structure')
   closed=True;break
  else:raise ValueError('unknown FDT token')
 if not closed:raise ValueError('FDT missing END token')
 return nodes

def audit_device_tree(raw,declaration,catalog_sha256,execution_sha256):
 """Audit actual DTB properties; declared boot identity is never measured HW."""
 nodes=read_fdt(raw)
 def cell(data):
  if not isinstance(data,bytes) or len(data)%4:raise ValueError('whole DT cells required')
  return list(struct.unpack('>'+str(len(data)//4)+'I',data))
 def one(data):
  values=cell(data)
  if len(values)!=1:raise ValueError('exact one DT cell required')
  return values[0]
 def string(data):
  if not isinstance(data,bytes) or not data.endswith(b'\0') or b'\0' in data[:-1]:raise ValueError('one terminated DT string required')
  try:return data[:-1].decode('ascii')
  except UnicodeDecodeError:raise ValueError('ASCII DT string required')
 def enabled(path):
  while path:
   if string(nodes[path].get('status',b'okay\0')) not in ('okay','ok'):return False
   path=path.rsplit('/',1)[0] if path!='/' else ''
   if path=='':break
  return True
 phandles={}
 for path,node in nodes.items():
  for key in ('phandle','linux,phandle'):
   if key not in node:continue
   value=one(node[key])
   if value in (0,0xffffffff) or (value in phandles and phandles[value]!=path):raise ValueError('duplicate or invalid DT phandle')
   if 'phandle' in node and 'linux,phandle' in node and node['phandle']!=node['linux,phandle']:raise ValueError('DT phandle aliases disagree')
   phandles[value]=path
 candidates=[p for p,n in nodes.items() if 'vortex,compute-v2' in n]
 if len(candidates)!=1:raise ValueError('exactly one explicit compute-v2 backend required')
 path=candidates[0];node=nodes[path];parent=path.rsplit('/',1)[0]
 if parent!='/amba_pl@0' or not enabled(path) or one(nodes[parent].get('#address-cells',b''))!=2 or one(nodes[parent].get('#size-cells',b''))!=2:raise ValueError('enabled PL backend with 64-bit address/size cells required')
 flag=declaration.get('provider_flags')
 from scripts.unified.cross_common import validate_provider_identity
 validate_provider_identity(declaration)
 if string(node.get('compatible',b''))!=declaration['compatible']:raise ValueError('DT compatible differs from selected provider')
 for key in ('vortex,compute-v2','vortex,ps-placement-approved'):
  if node.get(key)!=b'':raise ValueError('explicit boolean '+key+' required')
 expected={'hardware-contract':declaration['hardware_contract'],'bit':declaration['bit_sha256'],'xsa':declaration['xsa_sha256'],'catalog':catalog_sha256,'execution':execution_sha256}
 for name,digest in expected.items():
  if not isinstance(digest,str) or not re.fullmatch('[0-9a-f]{64}',digest) or node.get('vortex,'+name+'-sha256')!=bytes.fromhex(digest):raise ValueError('exact provider/catalog/execution digest required: '+name)
 registers=cell(node.get('reg',b''))
 if registers!=[0,0xa0000000,0,65536,0,0xb0010000,0,65536] or node.get('reg-names')!=b'gpu\0cdma\0':raise ValueError('frozen GPU/CDMA register resources required')
 # The generic generated HPM/CDMA nodes must not compete with this owner.
 for other,props in nodes.items():
  if other==path or other.rsplit('/',1)[0]!=parent or not enabled(other):continue
  values=cell(props.get('reg',b''))
  if len(values)%4:raise ValueError('PL sibling has ambiguous resource width')
  for i in range(0,len(values),4):
   start=(values[i]<<32)|values[i+1];length=(values[i+2]<<32)|values[i+3]
   if any(start<base+65536 and base<start+length for base in (0xa0000000,0xb0010000)):raise ValueError('another enabled PL owner overlaps GPU/CDMA')
 region=phandles.get(one(node.get('memory-region',b'')));pool=nodes.get(region,{})
 if not region or not region.startswith('/reserved-memory/') or not enabled(region) or string(pool.get('compatible',b''))!='shared-dma-pool' or pool.get('no-map')!=b'' or 'reusable' in pool:raise ValueError('dedicated no-map reserved DMA pool required')
 reserved=nodes.get('/reserved-memory',{})
 if one(reserved.get('#address-cells',b''))!=2 or one(reserved.get('#size-cells',b''))!=2 or reserved.get('ranges')!=b'':raise ValueError('untranslated 64-bit reserved-memory parent cells required')
 if cell(pool.get('reg',b''))!=[0,0x10000000,0,1048576] or (declaration['ps_base'],declaration['ps_bytes'])!=(0x10000000,1048576):raise ValueError('exact fixed PS DMA pool required')
 ready=cell(node.get('ready-gpios',b''));gpio_path=phandles.get(ready[0] if ready else None);gpio=nodes.get(gpio_path,{})
 if not ready or not gpio_path or not enabled(gpio_path) or len(ready)!=1+one(gpio.get('#gpio-cells',b'')) or cell(gpio.get('reg',b''))!=[0,0xb0020000,0,65536] or ready[1:]!=[0]*(len(ready)-1):raise ValueError('ready GPIO must resolve to frozen status resource')
 clocks=cell(node.get('clocks',b''));controller_path=phandles.get(clocks[0] if clocks else None);controller=nodes.get(controller_path,{})
 if clocks[1:]!=[71] or not controller_path or not enabled(controller_path) or one(controller.get('#clock-cells',b''))!=1 or node.get('clock-names')!=b'axi\0':raise ValueError('explicit PS PL0 clock reference required')
 return dict(scope='ACTUAL_BINARY_DT_BINDING_STATIC_NOT_PROBE_OR_BOARD',backend=path,provider_flags=flag,compatible=declaration['compatible'],dtb_sha256=hashlib.sha256(raw).hexdigest(),ps_pool=[0x10000000,1048576],pl_pool=[0x500000000,0x80000000],binding_digests=expected,identity_source='BOOT_MANIFEST_DECLARED',hardware_hash_measured=False,gpu_raw_cycles=None)

def audit_kernel(receipt):
 """Read the actual private build outputs, not caller-supplied PASS booleans."""
 from scripts.unified.cross_common import sha
 if receipt.get('scope')!='ACTUAL_PROVIDER_BUILTIN_KERNEL_AND_BINARY_DT_NOT_COMPLETE_BOOT_IMAGE' or receipt.get('natural_exit')!=0 or receipt.get('gpu_raw_cycles','missing') is not None or receipt.get('source_before')!=receipt.get('source_after'):raise ValueError('completed matching actual kernel component receipt required')
 declaration=receipt['provider'];project=pathlib.Path(receipt['project']).resolve();ks=pathlib.Path(receipt['kernel_source']).resolve();kb=pathlib.Path(receipt['kernel_build']).resolve()
 if ks==kb or project not in ks.parents or project not in kb.parents or receipt['hardware']['provider']!=declaration:raise ValueError('distinct private provider KS/KB and exact hardware declaration required')
 if not receipt['commands'] or any(c.get('natural_exit')!=0 or not pathlib.Path(c['log']).is_file() or sha(c['log'])!=c['log_sha256'] for c in receipt['commands']):raise ValueError('actual successful unchanged build command logs required')
 artifacts=receipt['artifacts']
 if set(artifacts)!={'vmlinux','Image','driver','config','symvers','dtb','symbols'}:raise ValueError('complete actual kernel/config/symbol/DT artifact set required')
 bundle=receipt.get('builtin_bundle',{})
 if bundle.get('scope')!='ACTUAL_BUILTIN_ARCHIVE_ALL_MEMBER_RELOCATABLE_EXTRACTION_NOT_MODULE_OR_GPU' or bundle.get('gpu_raw_cycles','missing') is not None or bundle.get('artifact')!=artifacts['driver']:raise ValueError('complete actual builtin archive extraction required; a module preview is not builtin evidence')
 from scripts.unified.bind_kernel import builtin_bundle_inputs
 members=bundle.get('members',[])
 if builtin_bundle_inputs(kb,[r['path'] for r in members])!=members:raise ValueError('actual builtin archive input identity differs')
 archive=pathlib.Path(bundle['archive']['path'])
 if archive!=kb/'drivers/misc/vxl/driver/built-in.a' or archive.is_symlink() or sha(archive)!=bundle['archive']['sha256'] or archive.read_bytes()[:8]!=b'!<thin>\n':raise ValueError('actual private builtin thin archive differs')
 if len(bundle.get('commands',[]))!=2 or any(c.get('natural_exit')!=0 or sha(c['log'])!=c['log_sha256'] for c in bundle['commands']):raise ValueError('actual archive listing and relocatable extraction commands required')
 if pathlib.Path(bundle['commands'][0]['log']).read_text().splitlines()!=[r['path'] for r in members]:raise ValueError('complete actual archive membership differs from extraction inputs')
 for name,row in artifacts.items():
  p=pathlib.Path(row['path']);before=p.lstat()
  if not stat.S_ISREG(before.st_mode) or before.st_size!=row['bytes'] or sha(p)!=row['sha256']:raise ValueError('actual kernel artifact changed: '+name)
  after=p.lstat()
  if (before.st_ino,before.st_size,before.st_mtime_ns)!=(after.st_ino,after.st_size,after.st_mtime_ns):raise ValueError('artifact mutated during kernel audit')
 for name,kind in [('vmlinux',2),('driver',1)]:
  with pathlib.Path(artifacts[name]['path']).open('rb') as f:header=f.read(64)
  if len(header)!=64 or header[:7]!=b'\x7fELF\x02\x01\x01' or struct.unpack_from('<HH',header,16)!=(kind,183):raise ValueError('actual AArch64 executable/builtin object required')
 with pathlib.Path(artifacts['Image']['path']).open('rb') as f:image_header=f.read(64)
 if len(image_header)!=64 or image_header[56:60]!=b'ARM\x64':raise ValueError('actual raw ARM64 Linux Image header required')
 config=pathlib.Path(artifacts['config']['path']).read_text().splitlines()
 for setting in ['CONFIG_VXL_LINUX=y','CONFIG_ARM64=y','CONFIG_OF_RESERVED_MEM=y','CONFIG_CMA=y','CONFIG_CRYPTO_SHA256=y','CONFIG_DEVTMPFS=y','CONFIG_DEVTMPFS_MOUNT=y']:
  if config.count(setting)!=1:raise ValueError('actual built-in kernel configuration missing: '+setting)
 release=(kb/'include/config/kernel.release').read_text().strip()
 if not release.startswith('5.4.0-xilinx-v2020.1'):raise ValueError('matched Linux5.4/PetaLinux2020.1 kernel release required')
 symbols={line.split()[-1] for line in pathlib.Path(artifacts['symbols']['path']).read_text().splitlines() if len(line.split())>=3}
 required={'vxl_init','__initcall_vxl_init6','vxl_fops','vxl_v2_linux_startup','vxl_v2_dispatch','vxl_v2_worker_locked','vxl_v2_close_locked','vxl_v2_execution_bind','vxl_v2_boot_bind','vxl_v2_native_execution_ops','vxl_s8_attach','vxl_hunet_attach','vxl_profile_setup','vxl_engine_init_profile'}
 if not required<=symbols:raise ValueError('actual builtin/legacy lifecycle symbols missing: '+repr(sorted(required-symbols)))
 # Every overlaid input must be the actual file seen by the private kernel.
 for row in receipt['kernel_input_files']:
  p=ks/'drivers/misc/vxl'/row['path']
  if not p.is_file() or p.stat().st_size!=row['bytes'] or sha(p)!=row['sha256']:raise ValueError('actual private kernel source differs from bound packet: '+row['path'])
 firmware=receipt['firmware'];catalog=firmware['catalog-v1.bin'];execution=firmware['execution-v1.bin']
 for row in firmware.values():
  if pathlib.Path(row['path']).stat().st_size!=row['bytes'] or sha(row['path'])!=row['sha256']:raise ValueError('bound actual firmware bytes changed')
 dt=audit_device_tree(pathlib.Path(artifacts['dtb']['path']).read_bytes(),declaration,catalog['sha256'],execution['sha256'])
 # Model data is deliberately excluded. This records all real Linux source
 # files, including the actual overlaid driver/core/headers, not a Git label.
 rows=[]
 for folder,dirs,files in os.walk(str(ks),followlinks=False):
  dirs[:]=sorted(d for d in dirs if d not in ('.git','__pycache__'))
  for name in sorted(dirs+files):
   p=pathlib.Path(folder,name);rel=p.relative_to(ks).as_posix()
   if p.is_symlink():
    if ks not in p.resolve().parents:raise ValueError('private kernel source has an external symlink')
    rows.append(dict(path=rel,kind='internal_symlink',target=os.readlink(str(p))))
   elif p.is_file():rows.append(dict(path=rel,bytes=p.stat().st_size,sha256=sha(p)))
 source_id=hashlib.sha256(b'VX_U23_ACTUAL_KERNEL_SOURCE_V1\0'+json.dumps(rows,sort_keys=True,separators=(',',':')).encode()).hexdigest()
 manifest=pathlib.Path(artifacts['symbols']['path']).parent/'actual-kernel-source-manifest.json'
 manifest.write_text(json.dumps(dict(scope='ACTUAL_PRIVATE_KERNEL_SOURCE_INVENTORY_NOT_GPU',kernel_source_id=source_id,files=rows,gpu_raw_cycles=None),indent=2)+'\n')
 return dict(scope='ACTUAL_AARCH64_BUILTIN_AND_DTB_COMPONENT_AUDIT_NOT_BOOT_OR_BOARD',provider_flags=declaration['provider_flags'],kernel_release=release,kernel_source_id=source_id,source_manifest=dict(path=str(manifest),bytes=manifest.stat().st_size,sha256=sha(manifest)),builtin_symbols=sorted(required),device_tree=dt,hardware_hash_measured=False,board_identity_verified=False,gpu_raw_cycles=None)

def parse_design_hwh(raw):
 if not isinstance(raw,bytes) or not 0<len(raw)<=12*1024**2 or b'<!DOCTYPE' in raw or b'<!ENTITY' in raw:
  raise ValueError('bounded plain HWH bytes required')
 try:doc=ET.fromstring(raw)
 except ET.ParseError as e:raise ValueError('invalid HWH XML: '+str(e))
 if doc.attrib.get('VIVADOVERSION')!='2020.1':raise ValueError('matched Vivado2020.1 required')
 modules=[m for m in doc.iter('MODULE') if 'zynq_ultra_ps_e' in str(m.attrib)]
 if len(modules)!=1:raise ValueError('exact one ZynqMP PS instance required')
 parameters={}
 for p in modules[0].findall('./PARAMETERS/PARAMETER'):
  name=p.attrib.get('NAME');value=p.attrib.get('VALUE')
  if not name or name in parameters:raise ValueError('unique PS parameter names required')
  # Vivado2020.1 leaves optional PCIe/QoS values unset in the actual XSA.
  # Retain JSON null; neither discard the entry nor invent zero/empty/default.
  parameters[name]=value
 if not parameters:raise ValueError('PS parameter set missing')
 ranges={}
 for m in doc.iter('MEMRANGE'):
  name=m.attrib.get('INSTANCE')
  if name not in ('HPM','cdma','status_gpio','ddr'):continue
  try:pair=(int(m.attrib['BASEVALUE'],16),int(m.attrib['HIGHVALUE'],16))
  except (KeyError,ValueError):raise ValueError('explicit hexadecimal address range required')
  if pair[1]<pair[0] or (name in ranges and ranges[name]!=pair):raise ValueError('conflicting/invalid duplicate address range')
  ranges[name]=pair
 expected={'HPM':(0xa0000000,0xa000ffff),'cdma':(0xb0010000,0xb001ffff),'status_gpio':(0xb0020000,0xb002ffff),'ddr':(0x500000000,0x5ffffffff)}
 if ranges!=expected:raise ValueError('frozen GPU/CDMA/ready/PL-DDR map differs')
 canonical=json.dumps(parameters,sort_keys=True,separators=(',',':')).encode()
 return dict(scope='HWH_STATIC_PLATFORM_MAP_NOT_KERNEL_OR_BOARD',vivado='2020.1',registers={'gpu':[0xa0000000,65536],'cdma':[0xb0010000,65536],'ready_gpio':[0xb0020000,65536]},ps_pool=[0x10000000,1048576],pl_pool=[0x500000000,0x80000000],pl_aperture=[0x500000000,0x100000000],ps_parameter_count=len(parameters),ps_parameters_sha256=hashlib.sha256(canonical).hexdigest(),gpu_raw_cycles=None)

def read_hardware_input(root,provider_flag,xsa_path,bit_path):
 from scripts.unified.cross_common import provider
 declaration=provider(root,provider_flag)
 def checked(path,expected):
  p=pathlib.Path(path);before=p.lstat()
  if p.is_symlink() or not stat.S_ISREG(before.st_mode) or not 0<before.st_size<=64*1024**2:raise ValueError('bounded regular hardware input required')
  raw=p.read_bytes();after=p.lstat();digest=hashlib.sha256(raw).hexdigest()
  if (before.st_ino,before.st_size,before.st_mtime_ns)!=(after.st_ino,after.st_size,after.st_mtime_ns) or digest!=expected:raise ValueError('frozen hardware input identity changed')
  return dict(path=str(p),bytes=len(raw),sha256=digest)
 xsa=checked(xsa_path,declaration['xsa_sha256']);bit=checked(bit_path,declaration['bit_sha256']);designs=[];members=[]
 with zipfile.ZipFile(xsa_path) as archive:
  infos=archive.infolist();names=[x.filename for x in infos]
  if len(set(names))!=len(names) or sum(x.file_size for x in infos)>256*1024**2:raise ValueError('ambiguous or oversized XSA archive')
  bits=[x for x in infos if x.filename.lower().endswith('.bit')]
  if len(bits)!=1 or bits[0].file_size!=bit['bytes'] or hashlib.sha256(archive.read(bits[0])).hexdigest()!=bit['sha256']:raise ValueError('embedded XSA bit differs from exact standalone bit')
  for info in infos:
   name=pathlib.PurePosixPath(info.filename)
   if name.is_absolute() or '..' in name.parts:raise ValueError('unsafe XSA member path')
   if not info.filename.lower().endswith('.hwh'):continue
   if info.file_size>12*1024**2:raise ValueError('oversized HWH member')
   data=archive.read(info);members.append(dict(path=info.filename,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
   # A delivered XSA also contains PMU and PS-internal HWH files. Select
   # the unique PL design by its actual GPU address, never by ZIP order.
   try:doc=ET.fromstring(data)
   except ET.ParseError:raise ValueError('invalid XSA HWH member')
   if any(x.attrib.get('INSTANCE')=='HPM' for x in doc.iter('MEMRANGE')):designs.append(dict(member=info.filename,platform=parse_design_hwh(data)))
 if len(designs)!=1:raise ValueError('unique matched PL design HWH required')
 return dict(scope='EXACT_BIT_XSA_STATIC_INPUTS_NOT_BOOT_KERNEL_OR_BOARD',provider=declaration,xsa=xsa,bit=bit,hwh_members=members,design=designs[0],board_identity_verified=False,gpu_raw_cycles=None)
