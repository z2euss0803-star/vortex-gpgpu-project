"""Actual ELF checks. Component builds cannot satisfy the full application gate."""
import argparse,hashlib,json,pathlib,struct
def validate_elf(raw, machine):
    if len(raw)<64 or raw[:6]!=b'\x7fELF\x02\x01':raise ValueError('ELF64 little endian required')
    kind,actual,version,entry,phoff,_,_,ehsize,phsize,phcount,_,_,_=struct.unpack_from('<HHIQQQIHHHHHH',raw,16)
    if actual!=machine or kind not in (2,3) or version!=1 or not entry or ehsize!=64 or phsize!=56 or not phcount or phoff<64 or phoff+phsize*phcount>len(raw):raise ValueError('ELF target/header/load table mismatch')
    loads=[]
    for i in range(phcount):
        ptype,flags,offset,address,_,filesz,memsz,align=struct.unpack_from('<IIQQQQQQ',raw,phoff+i*phsize)
        if ptype==1:
            if memsz<filesz or offset+filesz>len(raw) or address+memsz>=2**64:raise ValueError('ELF load segment invalid')
            loads.append(dict(address=address,memory_bytes=memsz,file_bytes=filesz,flags=flags,alignment=align))
    if not any(s['address']<=entry<s['address']+s['memory_bytes'] and s['flags']&1 for s in loads):raise ValueError('entry outside executable load')
    return dict(machine=actual,entry=entry,loads=loads)

def audit_cross(manifest):
    path=pathlib.Path(manifest);m=json.loads(path.read_text());base=path.parent
    if m.get('scope')!='PARTIAL_AARCH64_COMPONENTS_NOT_SYSTEM' or m.get('target')!='aarch64-xilinx-linux' or '9.2.0' not in m.get('compiler_version','') or m.get('raw_gpu_cycles') is not None:raise ValueError('matching compiler/component scope required')
    checked=[]
    for r in m['elfs']:
        p=pathlib.PurePosixPath(r['path'])
        if p.is_absolute() or '..' in p.parts or '\\' in str(p):raise ValueError('unsafe artifact reference')
        target=base.joinpath(*p.parts)
        if target.is_symlink():raise ValueError('artifact symlink')
        raw=target.read_bytes()
        if len(raw)!=r['bytes'] or hashlib.sha256(raw).hexdigest()!=r['sha256']:raise ValueError('actual ELF bytes changed')
        checked.append(dict(path=r['path'],elf=validate_elf(raw,183)))
    if not {'vxl_demo','vxl_dp_viewer'}<={pathlib.PurePosixPath(r['path']).name for r in m['elfs']}:raise ValueError('runner/viewer artifacts missing')
    return dict(status='ACTUAL_AARCH64_COMPONENTS_PASS_NOT_DRIVER_NOT_IMAGE_NOT_BOARD',checked=checked,raw_gpu_cycles=None,full_cross='NOT_COMPLETE_MOSAIC_NUMERIC_GATE_OPEN')

def audit_bound(bound,kernel,numeric):
    from scripts.demo.build_kernel import checked_bindings,sha
    from scripts.demo.dual_binding import source_digest
    from scripts.demo.provider_header import REQUIRED_FILES
    b=pathlib.Path(bound);k=pathlib.Path(kernel);r=json.loads((b/'receipt.json').read_text());kr=json.loads((k/'receipt.json').read_text());n=json.loads(pathlib.Path(numeric).read_text())
    if r.get('scope')!='ACTUAL_DUAL_BOUND_AARCH64_NOT_IMAGE_NOT_BOARD' or r.get('natural_exit')!=0 or kr.get('scope')!='ACTUAL_DUAL_BOUND_KERNEL_MODULE_NOT_IMAGE_NOT_BOARD' or kr.get('natural_exit')!=0:raise ValueError('actual matching CPU/kernel builds required')
    if not n.get('complete') or n.get('scope')!='HOST_FROZEN_W16A16_COMPAT_NOT_GPU_NOT_BOARD' or n.get('verified_cases')!=21 or n.get('verified_layer_outputs')!=336 or n.get('missing_cases') or sorted(x['row'] for x in n['records'])!=list(range(21)) or any(x['verified_layer_outputs']!=16 for x in n['records']):raise ValueError('complete independent frozen host numeric summary required')
    release=b/'release';manifest=json.loads((release/'release-manifest.json').read_text())
    if set(manifest['files'])!=REQUIRED_FILES|{'bin/vxl_demo','bin/vxl_dp_viewer','bin/demo-viewer-init','mosaic/binding.bin','hunet/binding-fused.bin','hunet/numeric-gate.bin','hunet/cpu/gelu.elf'}:raise ValueError('complete fixed release membership required')
    for name,row in manifest['files'].items():
        p=pathlib.PurePosixPath(name)
        if p.is_absolute() or '..' in p.parts or '\\' in name:raise ValueError('unsafe release member')
        q=release.joinpath(*p.parts)
        if q.is_symlink() or q.stat().st_size!=row['bytes'] or sha(q)!=row['sha256']:raise ValueError('actual release member changed '+name)
    for name,expected in (('vxl_demo',r['cpu_sha256']),('vxl_dp_viewer',r['viewer_sha256'])):
        p=release/'bin'/name
        if sha(p)!=expected:raise ValueError('actual executable changed')
        validate_elf(p.read_bytes(),183)
    if r['source_sha256']!=kr['source_sha256'] or r['source_sha256']!=source_digest(json.loads((b/'normalized-source.json').read_text())) or r['cpu_sha256']!=kr['cpu_sha256'] or r.get('formal_input_shape')!=[1,3,960,1280]:raise ValueError('bound source/CPU/formal geometry mismatch')
    m,h=checked_bindings((release/'mosaic/binding.bin').read_bytes(),(release/'hunet/binding-fused.bin').read_bytes(),r['cpu_sha256']);module=k/'source/driver/vxl_linux.ko';data=module.read_bytes()
    from scripts.demo.dual_binding import pending_hunet_gate
    if (release/'hunet/numeric-gate.bin').read_bytes()!=pending_hunet_gate(h) or sha(release/'hunet/cpu/gelu.elf')!=r['cpu_sha256']:raise ValueError('new CPU numeric gate or same-executable identity changed')
    if sha(module)!=kr['module_sha256'] or m not in data or h not in data:raise ValueError('actual linked module binding mismatch')
    rv=b/'rv32';rr=json.loads((rv/'receipt.json').read_text());elf=(rv/'kernel.elf').read_bytes()
    if rr.get('natural_exit')!=0 or elf[:6]!=b'\x7fELF\x01\x01' or int.from_bytes(elf[18:20],'little')!=243:raise ValueError('actual RV32 link missing')
    for name,key in (('code.bin','flat'),('norm-rodata.bin','rodata'),('kernel.elf','elf')):
        if (rv/name).stat().st_size!=rr[key]['bytes'] or sha(rv/name)!=rr[key]['sha256']:raise ValueError('actual RV32 payload changed')
    return dict(status='CROSS_PASS_NOT_BOARD',source_sha256=r['source_sha256'],cpu_sha256=r['cpu_sha256'],module_sha256=kr['module_sha256'],release_manifest_sha256=sha(release/'release-manifest.json'),host_numeric_summary_sha256=sha(numeric),old_program_ids='PRESERVED',gpu_raw_cycles=None,hardware='FROZEN_IDENTITY_REUSED_NOT_RETESTED',image='NOT_AUDITED',board='NOT_RUN',dp='NOT_RUN',hunet_measured_gate='PENDING_EXTERNAL_AUDIT')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--manifest');p.add_argument('--bound');p.add_argument('--kernel');p.add_argument('--numeric');a=p.parse_args()
    if a.bound:
        if not a.kernel or not a.numeric:p.error('--bound requires --kernel and --numeric')
        r=audit_bound(a.bound,a.kernel,a.numeric)
    elif a.manifest:r=audit_cross(a.manifest)
    else:p.error('--manifest or --bound required')
    print(json.dumps(r,sort_keys=True))
