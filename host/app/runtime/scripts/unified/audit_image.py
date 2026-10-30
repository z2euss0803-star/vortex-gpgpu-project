"""Inspect actual install/boot bytes. A build is never board/display acceptance."""
import hashlib,pathlib,re,zlib,json,stat,subprocess,sys,gzip,copy

def fit_payloads(raw):
 """Read the binary selected FIT config and independently hash inline data."""
 from scripts.unified.audit_kernel import read_fdt
 nodes=read_fdt(raw,max_bytes=512*1024**2)
 def string(value):
  if not isinstance(value,bytes) or not value.endswith(b'\0') or b'\0' in value[:-1]:raise ValueError('exact FIT string required')
  return value[:-1].decode('ascii')
 default=string(nodes.get('/configurations',{}).get('default'))
 if '/' in default or default not in ('conf@system-top.dtb','conf-system-top.dtb'):raise ValueError('explicit system-top FIT default configuration required')
 config=nodes.get('/configurations/'+default,{})
 result={}
 for role,kind,compression in [('kernel','kernel','gzip'),('fdt','flat_dt','none'),('ramdisk','ramdisk','gzip')]:
  name=string(config.get(role));path='/images/'+name
  if '/' in name or path not in nodes:raise ValueError('selected FIT payload reference differs')
  props=nodes[path]
  accepted_compression=('none','gzip') if role=='ramdisk' else (compression,)
  if string(props.get('type'))!=kind or string(props.get('arch'))!='arm64' or string(props.get('compression')) not in accepted_compression:raise ValueError('FIT payload type/architecture/compression differs')
  if role!='fdt' and string(props.get('os'))!='linux':raise ValueError('FIT operating system differs')
  data=props.get('data')
  if not isinstance(data,bytes) or not data or any(k in props for k in ('data-position','data-offset','data-size')):raise ValueError('bounded inline FIT data required')
  children=[v for p,v in nodes.items() if p.rsplit('/',1)[0]==path]
  if len(children)!=1 or string(children[0].get('algo'))!='sha256' or children[0].get('value')!=hashlib.sha256(data).digest():raise ValueError('one complete SHA256 proof per selected FIT data payload required')
  if role=='kernel':
   for key in ('load','entry'):
    value=props.get(key,b'')
    if len(value) not in (4,8) or int.from_bytes(value,'big')!=0x80000:raise ValueError('ARM64 kernel placement differs')
   # Bound decompression before trusting any expanded kernel bytes.
   decoder=zlib.decompressobj(31);expanded=decoder.decompress(data,0x10000000-0x80000)
   if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:raise ValueError('kernel gzip exceeds bounded load window or has trailing data')
   result['kernel']=expanded
  elif role=='fdt':result['dtb']=data
  else:
   # PetaLinux 2020.1 embeds .cpio.gz with compression=none: U-Boot
   # passes the gzip stream intact and Linux expands the initramfs.
   # A correct FIT hash must not admit a different container format.
   if not data.startswith(b'\x1f\x8b\x08'):raise ValueError('gzip initramfs container required regardless of FIT decompression metadata')
   result['rootfs_gzip']=data
 return result

def normalize_cpio_entries(entries):
 """Keep actual archive kinds, especially directories, for payload admission."""
 normalized={}
 for name,row in entries.items():
  item=dict(row);item['kind']={stat.S_IFREG:'file',stat.S_IFDIR:'directory',stat.S_IFLNK:'symlink'}.get(row['kind'],'other')
  if item['kind']=='symlink':item['link']=row['data'].decode('utf8')
  normalized[name]=item
 return normalized


def model_directory_errors(entries,expected,installation):
 """Require package roots and every nested directory independently of hashes."""
 errors=[];prefix='usr/share/vxgpu/models/';roots=set()
 for name in set(expected)|set(entries):
  if name.startswith(prefix):roots.add(prefix+name[len(prefix):].split('/')[0])
 try:
  declared=json.loads(installation).get('nn',[])
  if not isinstance(declared,list):raise ValueError('NN package list required')
  for row in declared:
   path=row['package']
   if not isinstance(path,str) or not re.fullmatch('/'+prefix+r'[A-Za-z0-9_-][A-Za-z0-9_.-]*',path):raise ValueError('canonical model package root required')
   roots.add(path[1:])
 except (ValueError,KeyError,TypeError,AttributeError):return ['model directory installation declaration']
 directories=set();ancestors=set()
 for root in roots:
  directories.add(root)
  ancestors.update(str(p) for p in pathlib.PurePosixPath(root).parents if str(p)!='.')
  for name in set(expected)|set(entries):
   if not name.startswith(root+'/'):continue
   path=pathlib.PurePosixPath(name)
   if str(path)!=name or '..' in path.parts:errors.append('model directory unsafe member '+name);continue
   directories.update(str(p) for p in path.parents if str(p)==root or str(p).startswith(root+'/'))
   if name in entries and entries[name].get('kind')!='file':directories.add(name)
 for name in sorted(directories):
  row=entries.get(name,{});mode=row.get('mode')
  if (row.get('kind')!='directory' or type(row.get('uid')) is not int or row['uid']!=0 or
      type(row.get('gid')) is not int or row['gid']!=0 or type(mode) is not int or
      mode&0o7222 or mode&0o500!=0o500):errors.append('model directory root-owned read/search-only non-link '+name)
 for name in sorted(ancestors-directories):
  row=entries.get(name,{});mode=row.get('mode')
  if (row.get('kind')!='directory' or row.get('uid')!=0 or row.get('gid')!=0 or
      type(mode) is not int or mode&0o7022 or mode&0o500!=0o500):errors.append('model directory safe searchable ancestor '+name)
 return errors


def payload_errors(entries,manifest):
 """Compare rootfs entries to sealed payload and independent startup policy.

 This checks installation bytes/ownership and startup semantics only. Real
 target ELF/DT/FIT/BOOT inspection is an additional, mandatory image gate.
 """
 errors=[]
 if (type(manifest.get('schema')) is not int or manifest['schema']!=1 or
     manifest.get('scope')!='MATCHED_IMAGE_PAYLOAD_NOT_EXECUTED' or
     type(manifest.get('provider_flags')) is not int or manifest['provider_flags'] not in (1,2,4) or
     manifest.get('gpu_raw_cycles','missing') is not None):errors.append('declared isolated offline payload scope required')
 build_id=manifest.get('build_id','')
 if not isinstance(build_id,str) or not re.fullmatch('[A-Za-z0-9_.-]{1,128}',build_id):errors.append('bounded declared build-id required')
 required={'etc/codex-build-id','etc/default/vxgpud','etc/vxgpud/config.json','etc/vxgpud/installation.json','etc/vxgpud/installation.sha256','etc/init.d/codex-direct-network','etc/network/interfaces','etc/init.d/vxgpud','usr/bin/vxgpud'}
 expected=manifest.get('files',{})
 if not isinstance(expected,dict) or not required<=set(expected):return errors+['complete payload installation manifest required']
 for name,want in expected.items():
  path=pathlib.PurePosixPath(name)
  if path.is_absolute() or '..' in path.parts or '.' in path.parts or str(path)!=name or not isinstance(want,dict):errors.append('unsafe payload path '+name);continue
  actual=entries.get(name,{})
  if actual.get('kind')!='file':errors.append('regular installed payload '+name);continue
  raw=actual.get('data');mode=actual.get('mode')
  if not isinstance(raw,bytes) or len(raw)!=want.get('bytes') or hashlib.sha256(raw).hexdigest()!=want.get('sha256'):errors.append('actual payload bytes '+name)
  if actual.get('uid')!=0 or actual.get('gid')!=0 or mode!=want.get('mode') or type(mode) is not int or mode&0o7022:errors.append('root-owned non-writable payload mode '+name)
  if name=='etc/vxgpud/config.json' and mode!=0o600:errors.append('same-owner0600 startup configuration '+name)
  if name in ('etc/vxgpud/installation.json','etc/vxgpud/installation.sha256') and mode not in (0o400,0o444):errors.append('immutable0400/0444 installation manifest/seal '+name)
 def raw(name):
  value=entries.get(name,{}).get('data',b'');return value if isinstance(value,bytes) else b''
 errors+=model_directory_errors(entries,expected,raw('etc/vxgpud/installation.json'))
 if raw('etc/codex-build-id')!=(build_id+'\n').encode():errors.append('exact installed build-id')
 if raw('etc/default/vxgpud')!=b'UNIFIED_SERVICE_ENABLED=0\n':errors.append('service must default OFF; no GPU tasks at boot')
 init=raw('etc/init.d/vxgpud')
 if any(token not in init for token in (b'/etc/default/vxgpud',b'"$UNIFIED_SERVICE_ENABLED" = 1',b'/usr/bin/vxgpud --serve /etc/vxgpud/config.json')):errors.append('explicit opt-in controlled service startup')
 network=raw('etc/init.d/codex-direct-network')
 if any(token not in network for token in (b'ip link set dev eth0 up',b'ip -4 addr show dev eth0',b'ip addr add 192.168.13.10/24 dev eth0')) or b' addr replace ' in network or b'dhclient' in network or b'udhcpc' in network:errors.append('idempotent BusyBox-compatible direct static network')
 interfaces=raw('etc/network/interfaces')
 if not re.search(rb'(?m)^iface eth0 inet static\s*$',interfaces) or re.search(rb'(?m)^iface eth0 inet dhcp\s*$',interfaces) or b'192.168.13.10' not in interfaces or b'255.255.255.0' not in interfaces:errors.append('static eth0 and no competing DHCP stanza')
 expected_links={'etc/rcS.d/S40codex-direct-network':'../init.d/codex-direct-network'}
 # SysV S95 at multiuser runlevels is safe only because the service defaults OFF.
 if entries.get('etc/rc5.d/S95vxgpud',{}).get('link')!='../init.d/vxgpud':errors.append('explicit late service init binding')
 for name,link in expected_links.items():
  node=entries.get(name,{})
  if node.get('kind')!='symlink' or node.get('link')!=link or node.get('uid')!=0:errors.append('network init binding/order '+name)
 for name,node in entries.items():
  p=pathlib.PurePosixPath(name)
  if len(p.parts)!=3 or p.parts[0]!='etc' or not re.fullmatch(r'rc(?:S|[2-5])\.d',p.parts[1]) or not p.name.startswith('S'):continue
  if re.search(r'(?:demo-viewer|vxl-demo|board[-_]check|check[-_]board|alinx)',p.name,re.I):errors.append('legacy competing GPU/display autostart '+name)
  if node.get('link')=='../init.d/vxgpud' and (p.parts[1]=='rcS.d' or p.name!='S95vxgpud'):errors.append('premature common owner startup '+name)
 if manifest.get('provider_flags')==4:errors+=joint_payload_errors(raw)
 return errors

def joint_nn_ids_valid(ids):
 """Exact original two or complete explicit R10 nine; duplicates never count."""
 if type(ids) is not list or len(ids) not in (2,9) or any(type(name) is not str for name in ids):return False
 return tuple(sorted(ids)) in (
  ('hunet','mosaic'),
  ('hunet','hunet-gn16','hunet-gn9','mosaic','mosaic-b1024','mosaic-b4096','mosaic-profile','mosaic-smoke','mosaic-smoke-profile'))


def joint_payload_errors(raw):
 """Bind one joint installation to the exact S8 identity and all16 payloads."""
 import struct
 from scripts.unified.bind_kernel import catalog_entries
 from scripts.unified.cross_common import validate_provider_identity
 try:
  config=json.loads(raw('etc/vxgpud/config.json'));installed=raw('etc/vxgpud/installation.json');installation=json.loads(installed);hw=installation['hardware']
  want=dict(provider_flags=4,compatible='vortex,hw4-s8-joint-v1',hardware_contract=hw['hardware_contract'],bit_sha256=hw['bit'],xsa_sha256=hw['xsa']);validate_provider_identity(want)
  expected=dict(hardware_contract=want['hardware_contract'],bit=want['bit_sha256'],xsa=want['xsa_sha256'],compatible=hashlib.sha256(want['compatible'].encode()).hexdigest(),provider_flags=4)
  if hw!=expected or any(config.get(k)!=v for k,v in expected.items()) or config.get('backend_mode')!='compute-v2':raise ValueError('joint config and installation exact S8 identity')
  if raw('etc/vxgpud/installation.sha256')!=(hashlib.sha256(installed).hexdigest()+'\n').encode():raise ValueError('actual installation seal')
  if installation.get('version')!=2 or not joint_nn_ids_valid([x['id'] for x in installation['nn']]) or any(not isinstance(installation.get(k),dict) or not installation[k] for k in ('image','p8','mosaic_export')):raise ValueError('all NN/IMAGE/P8 services and original Mosaic exporter required')
  cat=raw('usr/share/vxgpu/catalog.bin');rows=catalog_entries(cat)
  if len(rows)!=16 or [(r['fields'][0],r['fields'][1],r['fields'][2]) for r in rows]!=[(1,1,65552),(1,1,65568)]+[(3,256,512+i) for i in range(10)]+[(2,5,i) for i in (9,10,11,12)]:raise ValueError('exact joint16 programs; standalone INT16 demo excluded')
  if installation.get('catalog')!=dict(path='/usr/share/vxgpu/catalog.bin',sha256=hashlib.sha256(cat).hexdigest()) or raw('lib/firmware/vxl/catalog-v1.bin')!=cat:raise ValueError('user/kernel catalog binding')
  identity=[bytes.fromhex(want[k]) for k in ('hardware_contract','bit_sha256','xsa_sha256')]
  exe=raw('lib/firmware/vxl/execution-v1.bin')
  if len(exe)!=4160 or struct.unpack_from('<4I',exe)!=(0x454c5856,2,4160,16) or exe[16:48]!=hashlib.sha256(cat).digest():raise ValueError('joint execution catalog binding')
  for i,r in enumerate(rows):
   f=r['fields'];d=r['digests'];payload=raw('lib/firmware/vxl/programs/'+d[6].hex()+'.bin')
   if f[14]!=4 or d[3:6]!=identity or hashlib.sha256(r['schema']).digest()!=d[2] or len(payload)!=max(f[4]+f[5],f[6]+f[7]) or hashlib.sha256(payload[f[4]:f[4]+f[5]]).digest()!=d[0] or hashlib.sha256(payload[f[6]:f[6]+f[7]]).digest()!=d[1] or exe[64+256*i:96+256*i]!=d[6]:raise ValueError('joint original code/rodata/schema and execution entry binding')
  return []
 except (ValueError,KeyError,TypeError,struct.error) as error:return ['joint payload: '+str(error)]

def client_runtime_errors(files):
 """Installed Python client closure only; no target or device execution."""
 errors=[];client=files.get('usr/bin/vxgpuctl',b'')
 if not isinstance(client,bytes) or not client.startswith(b'#!/usr/bin/env python3\n'):errors.append('actual Python3 control client shebang required')
 def aarch64(raw):return isinstance(raw,bytes) and len(raw)>=64 and raw[:7]==b'\x7fELF\x02\x01\x01' and int.from_bytes(raw[18:20],'little')==183
 interpreters=[raw for name,raw in files.items() if re.fullmatch(r'usr/bin/\.?python3(?:\.7m?)?(?:[.-]real)?',name) and aarch64(raw)]
 if not interpreters or not files.get('usr/bin/python3'):errors.append('installed actual AArch64 Python3 interpreter/entry required')
 prefix='usr/lib/python3.7/'
 for name in ('argparse.py','json/__init__.py','pathlib.py','socket.py','struct.py','os.py','stat.py','encodings/__init__.py','codecs.py','collections/__init__.py','re.py','hashlib.py'):
  if not files.get(prefix+name):errors.append('installed control-client standard module '+name)
 for name in ('_socket','_struct'):
  matches=[raw for path,raw in files.items() if re.fullmatch(re.escape(prefix+'lib-dynload/'+name)+r'(?:\.[A-Za-z0-9_-]+)*\.so',path)]
  if len(matches)!=1 or not aarch64(matches[0]):errors.append('installed AArch64 control-client native extension '+name)
 hashes=[raw for path,raw in files.items() if re.fullmatch(re.escape(prefix+'lib-dynload/')+r'(?:_hashlib|_sha256)(?:\.[A-Za-z0-9_-]+)*\.so',path)]
 if not hashes or any(not aarch64(raw) for raw in hashes):errors.append('installed AArch64 SHA256 support for sealed joint file transfer')
 return errors

def image_source_provenance(root,built,payload,producer_root=None,producer_lock=None,producer_lock_sha256=None):
 """Separate a real frozen producer from a content checker, never rebuild labels."""
 import ast
 from scripts.unified.cross_common import snapshot,verify_source_lock
 root=pathlib.Path(root).resolve();current=snapshot(root)
 supplied=(producer_root,producer_lock,producer_lock_sha256)
 if any(x is not None for x in supplied):
  if any(x is None for x in supplied):raise ValueError('complete explicit frozen producer root/lock/digest required')
  producer_root=pathlib.Path(producer_root).resolve();producer=verify_source_lock(producer_root,producer_lock,producer_lock_sha256)
 else:producer_root=root;producer=current
 canonical=lambda value:json.dumps(value,sort_keys=True,separators=(',',':'))
 if any(canonical(rec.get(name))!=canonical(producer) for rec in (built,payload) for name in ('source_before','source_after')):raise ValueError('unchanged exact build and payload producer source required')
 old={r['path']:r for r in producer['files']};new={r['path']:r for r in current['files']}
 if set(old)!=set(new):raise ValueError('producer/auditor file membership differs')
 changed=sorted(p for p in old if old[p]!=new[p])
 allowed={'scripts/unified/image.py','scripts/unified/audit_image.py','tests/unified/test_image_boot.py','tests/unified/test_image_runtime_closure.py'}
 if set(changed)-allowed:raise ValueError('non-checker source changed after actual image production')
 def tree(base,path):return ast.parse((base/path).read_text())
 if 'scripts/unified/image.py' in changed:
  def projection(base):
   module=tree(base,'scripts/unified/image.py')
   module.body=[n for n in module.body if not (isinstance(n,ast.FunctionDef) and n.name=='boot_bif') and not (isinstance(n,ast.If) and any(isinstance(x,ast.Name) and x.id=='__name__' for x in ast.walk(n.test)))]
   return ast.dump(module)
  if projection(root)!=projection(producer_root):raise ValueError('actual image producer behavior changed outside independent BIF metadata')
 if 'scripts/unified/audit_image.py' in changed:
  def policy(base):
   nodes=[n for n in tree(base,'scripts/unified/audit_image.py').body if isinstance(n,ast.FunctionDef) and n.name in ('payload_errors','model_directory_errors')]
   if sum(n.name=='payload_errors' for n in nodes)!=1 or sum(n.name=='model_directory_errors' for n in nodes)>1:raise ValueError('one original installed payload policy required')
   return [ast.dump(n) for n in nodes]
  if policy(root)!=policy(producer_root):raise ValueError('installed payload admission policy changed after production')
 return dict(producer_source_root=str(producer_root),producer_source_snapshot_id=producer['source_snapshot_id'],auditor_source_root=str(root),auditor_source_snapshot_id=current['source_snapshot_id'],runtime_source_id=producer['runtime_source_id'],changed_checker_paths=changed,producer_rebuilt=False)

def audit_image(env_path,bound,payload_path,image_receipt,out):
 """Re-extract real FIT/rootfs and verify the complete matched boot inputs."""
 from scripts.unified.cross_common import sha,new_build_output,snapshot
 from scripts.unified.audit_kernel import audit_kernel,audit_device_tree,read_hardware_input
 env=json.loads(pathlib.Path(env_path).read_text());root=pathlib.Path(env['root']).resolve();out=new_build_output(root,out)
 original=json.loads((pathlib.Path(bound)/'receipt.json').read_text());built=json.loads(pathlib.Path(image_receipt).read_text());payload=json.loads((pathlib.Path(payload_path)/'receipt.json').read_text());project=pathlib.Path(original['project']);im=project/'images/linux'
 rec=dict(scope='ACTUAL_PROVIDER_COMPLETE_BOOT_FIT_ROOTFS_CONTENT_NOT_BOARD_OR_APPLICATION_PASS',commands=[],errors=[],gpu_raw_cycles=None,board='NOT_RUN',display='NOT_RUN',application_precision='NOT_RUN',performance='NOT_MEASURED',sd='NOT_TOUCHED')
 errors=rec['errors']
 try:
  producer_args=[env.get(k) for k in ('audit_producer_source_root','audit_producer_source_lock','audit_producer_source_lock_sha256')]
  rec['source_provenance']=image_source_provenance(root,built,payload,*producer_args)
  if type(built.get('natural_exit')) is not int or built['natural_exit']!=0 or type(payload.get('natural_exit')) is not int or payload['natural_exit']!=0 or built.get('provider')!=original['provider'] or built.get('gpu_raw_cycles','missing') is not None or built['kernel_receipt_sha256']!=sha(pathlib.Path(bound)/'receipt.json') or built['payload_receipt_sha256']!=sha(pathlib.Path(payload_path)/'receipt.json'):raise ValueError('successful exact complete build, frozen source and original component receipts required')
  for command in built['commands']:
   if command.get('natural_exit')!=0 or not pathlib.Path(command['log']).is_file() or sha(command['log'])!=command['log_sha256']:raise ValueError('actual successful unchanged complete build logs required')
  flag=original['provider']['provider_flags'];hardware=env['provider_hardware'][str(flag)];rec['hardware']=read_hardware_input(root,flag,hardware['xsa'],hardware['bit'])
  raw_fit=(im/'image.ub').read_bytes();fit=fit_payloads(raw_fit)
  if fit['kernel']!=(im/'Image').read_bytes() or fit['dtb']!=(im/'system.dtb').read_bytes() or fit['rootfs_gzip']!=(im/'rootfs.cpio.gz').read_bytes():raise ValueError('actual extracted FIT kernel/DT/rootfs differ from built component bytes')
  if not 0<len(raw_fit)<0x20000000 or not 0<len(fit['kernel'])<0x10000000-0x80000:raise ValueError('boot load window overlap or extent')
  for key,value in fit.items():(out/(key+'.bin')).write_bytes(value)
  rec['fit_payloads']={k:dict(bytes=len(v),sha256=hashlib.sha256(v).hexdigest()) for k,v in fit.items()}
  # Re-audit actual kernel/DT outputs after the complete image build, including
  # a legitimate kernel rebuild if the rootfs recipe affects initramfs inputs.
  # Keep the old U23 hashes and record every difference; do not relabel it.
  rebound=copy.deepcopy(original);artifacts=rebound['artifacts'];previous={k:r['sha256'] for k,r in artifacts.items()}
  from scripts.unified.bind_kernel import bundle_builtin_driver
  bundle=bundle_builtin_driver(rebound['kernel_build'],env['aarch64_prefix'],out/'complete-builtin-driver-evidence');rebound['builtin_bundle']=bundle;artifacts['driver']=dict(bundle['artifact']);rec['commands'].extend(bundle['commands'])
  for key,row in artifacts.items():
   if key=='symbols':continue
   path=pathlib.Path(row['path']);row.update(bytes=path.stat().st_size,sha256=sha(path))
  symbols=out/'complete-vmlinux.nm';argv=[env['aarch64_prefix']+'nm','--defined-only',artifacts['vmlinux']['path']]
  with symbols.open('xb') as f:p=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT)
  rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,log=str(symbols),log_sha256=sha(symbols)))
  if p.returncode:raise ValueError('actual complete built-in symbols unavailable')
  artifacts['symbols']=dict(path=str(symbols),bytes=symbols.stat().st_size,sha256=sha(symbols));rebound['commands']+=rec['commands'];rec['kernel_audit']=audit_kernel(rebound)
  if sha(im/'Image')!=artifacts['Image']['sha256']:raise ValueError('deployed FIT kernel differs from the actual private kernel build Image')
  rec['kernel_component_changes']={k:dict(U23_sha256=previous[k],complete_image_sha256=r['sha256']) for k,r in artifacts.items() if k!='symbols' and r['sha256']!=previous[k]}
  (out/'complete-kernel-rebound-receipt.json').write_text(json.dumps(rebound,indent=2)+'\n')
  for setting in ('CONFIG_DRM=y','CONFIG_DRM_XLNX=y','CONFIG_DRM_ZYNQMP_DPSUB=y','CONFIG_DRM_GEM_CMA_HELPER=y','CONFIG_DMA_CMA=y','CONFIG_CMA_SIZE_MBYTES=256'):
   if setting not in pathlib.Path(artifacts['config']['path']).read_text().splitlines():raise ValueError('actual local DisplayPort kernel config missing '+setting)
  rec['device_tree']=audit_device_tree(fit['dtb'],original['provider'],original['firmware']['catalog-v1.bin']['sha256'],original['firmware']['execution-v1.bin']['sha256'])
  # Import only the existing bounded archive/BOOT readers, not old NN IDs or
  # old acceptance rules. The selected provider is checked independently above.
  hunet_dir=root/'scripts/hunet';sys.path.insert(0,str(hunet_dir))
  from scripts.hunet import image_audit as legacy
  parser=legacy.legacy_cpio();cpio=gzip.decompress(fit['rootfs_gzip']);entries=parser.cpio_entries(cpio)
  recipe=project/'components/yocto/layers/core/meta/classes/image_types.bbclass';errors+=parser.rootfs_payload_errors(cpio,im/'rootfs.tar.gz',sha(recipe));errors+=legacy.ssh_entry_errors(entries)
  normalized=normalize_cpio_entries(entries)
  actual_manifest=normalized.get('usr/share/vxgpu/payload-manifest.json',{}).get('data',b'')
  if hashlib.sha256(actual_manifest).hexdigest()!=payload['payload_manifest_sha256']:raise ValueError('actual rootfs installed payload manifest differs')
  manifest=json.loads(actual_manifest);errors+=payload_errors(normalized,manifest)
  files=legacy.resolved_regular_files(entries);errors+=client_runtime_errors(files);cross=json.loads(pathlib.Path(env['aarch64_receipt']).read_text())
  for name,row in cross['target_runtime']['files'].items():
   raw=files.get(row['path'],b'')
   if hashlib.sha256(raw).hexdigest()!=row['sha256'] or raw[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(raw[18:20],'little')!=183:errors.append('actual target runtime library closure '+name)
  daemon=files.get('usr/bin/vxgpud',b'');wanted=next(r for r in cross['installation'] if r['destination']=='/usr/bin/vxgpud')
  if hashlib.sha256(daemon).hexdigest()!=wanted['sha256']:errors.append('actual production daemon differs from matched cross receipt')
  for name,row in original['firmware'].items():
   if hashlib.sha256(files.get('lib/firmware/vxl/'+name,b'')).hexdigest()!=row['sha256']:errors.append('actual boot-time firmware differs '+name)
  for name,row in built['components'].items():
   path=pathlib.Path(row['path']);raw=path.read_bytes();machine=189 if name=='pmufw.elf' else 183;endian='little' if raw[5:6]==b'\1' else 'big'
   if hashlib.sha256(raw).hexdigest()!=row['sha256'] or raw[:4]!=b'\x7fELF' or int.from_bytes(raw[18:20],endian)!=machine:errors.append('actual matching boot CPU component '+name)
  boot=(im/'BOOT.BIN').read_bytes();errors+=legacy.boot_bit_errors(boot,legacy.bit_payload(pathlib.Path(hardware['bit']).read_bytes()));errors+=legacy.boot_script_errors((im/'boot.scr').read_bytes())
  repeated=pathlib.Path(image_receipt).parent/'verification.BOOT.BIN'
  if sha(repeated)!=sha(im/'BOOT.BIN') or built['boot_reassembly']['boot_sha256']!=sha(im/'BOOT.BIN'):errors.append('actual independent complete boot reassembly no longer matches')
  if errors:raise ValueError('actual complete image audit failed: '+repr(errors))
  if image_source_provenance(root,built,payload,*producer_args)!=rec['source_provenance']:raise ValueError('producer or checker source changed during complete content audit')
  rec.update(natural_exit=0,build_id=payload['build_id'],provider=original['provider'],files={n:dict(bytes=(im/n).stat().st_size,sha256=sha(im/n)) for n in ('BOOT.BIN','boot.scr','image.ub','Image','system.dtb','rootfs.cpio.gz','rootfs.tar.gz')},payload_manifest_sha256=payload['payload_manifest_sha256'],complete_build_receipt_sha256=sha(image_receipt),hardware_hash_measured=False,board_identity_verified=False)
 except BaseException as error:rec.update(natural_exit=1,error=repr(error));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
 return rec
