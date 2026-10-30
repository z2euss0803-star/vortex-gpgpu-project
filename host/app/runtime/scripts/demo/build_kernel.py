"""Compile a private kernel module with both complete application identities.

No module insertion, shared kernel edits, device or removable-media operations.
"""
import argparse,hashlib,json,pathlib,re,shutil,struct,subprocess
from scripts.demo.dual_binding import inventory_source,source_digest,_checked_hunet
from scripts.mosaic.binding import ORDER,make_binding

def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()

def checked_bindings(mosaic,hunet,cpu):
    if len(mosaic)!=512:raise ValueError('complete program12 binding required')
    values={n:mosaic[32+i*32:64+i*32].hex() for i,n in enumerate(ORDER)}
    if make_binding(values)!=mosaic or values['runner']!=cpu:raise ValueError('program12 schema or CPU mismatch')
    _checked_hunet(hunet)
    if hunet[256:288].hex()!=cpu:raise ValueError('program9 CPU mismatch')
    return mosaic,hunet

def build(env_path,root,out,bound):
    e=json.loads(pathlib.Path(env_path).read_text());root=pathlib.Path(root).resolve();out=pathlib.Path(out).resolve();out.mkdir(parents=True,exist_ok=False);bound=pathlib.Path(bound).resolve()
    rec=dict(scope='ACTUAL_DUAL_BOUND_KERNEL_MODULE_NOT_IMAGE_NOT_BOARD',natural_exit=1,gpu_raw_cycles=None)
    try:
        metadata=json.loads((bound/'receipt.json').read_text())
        if metadata.get('natural_exit')!=0 or metadata.get('scope')!='ACTUAL_DUAL_BOUND_AARCH64_NOT_IMAGE_NOT_BOARD':raise ValueError('completed actual bound CPU build required')
        anchor=source_digest(inventory_source(root))
        if anchor!=metadata['source_sha256']:raise ValueError('kernel/CPU source mismatch')
        cpu=bound/'release/bin/vxl_demo'
        if sha(cpu)!=metadata['cpu_sha256']:raise ValueError('actual CPU changed')
        mosaic,hunet=checked_bindings((bound/'release/mosaic/binding.bin').read_bytes(),(bound/'release/hunet/binding-fused.bin').read_bytes(),sha(cpu))
        kb=pathlib.Path(e['kernel_build']);kernel=pathlib.Path(e['kernel_source']);prefix=e['aarch64_prefix'];release=(kb/'include/config/kernel.release').read_text().strip()
        before={str(p):sha(p) for p in (kb/'.config',kb/'Module.symvers')}
        if not release.startswith('5.4.0-xilinx-v2020.1') or mosaic[352:384].hex()!=sha(kb/'.config'):raise ValueError('matching kernel config/release required')
        private=out/'source';private.mkdir()
        for name in ('driver','core','include','generated'):
            shutil.copytree(str(root/name),str(private/name),ignore=shutil.ignore_patterns('__pycache__','*.o','*.ko','*.cmd','Module.symvers','modules.order'))
        headers=json.loads((bound/'kernel-headers.json').read_text())
        if set(headers)!={'vxl_mosaic_release.h','vxl_fused_release.h','vxl_hybrid_release.h','vxl_hunet_source.h','vxl_s8_release.h'}:raise ValueError('exact old/new release headers required')
        for name,value in headers.items():
            p=bound/'kernel-headers'/name
            if sha(p)!=value:raise ValueError('bound header changed')
            shutil.copyfile(str(p),str(private/'driver'/name))
        cmd=['make','-j1','-C',str(kernel),'O='+str(kb),'ARCH=arm64','CROSS_COMPILE='+prefix,'M='+str(private/'driver'),'W=1','modules']
        q=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/'build.log').write_bytes(q.stdout);(out/'build.exit').write_text(str(q.returncode)+'\n');rec.update(command=cmd,build_natural_exit=q.returncode,natural_exit=q.returncode)
        if q.returncode:raise ValueError('bound module compile failed; see build.log')
        module=private/'driver/vxl_linux.ko';data=module.read_bytes()
        if data[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',data,18)[0]!=183 or mosaic not in data or hunet not in data:raise ValueError('actual module architecture or full bindings missing')
        nm=subprocess.check_output([prefix+'nm','--defined-only',str(module)]);(out/'module.nm').write_bytes(nm)
        for name in ('vxl_s8_attach','vxl_mosaic_attach','mosaic_program_register','vxl_fused_init'):
            if not re.search(rb'\b'+name.encode()+rb'$',nm,re.M):raise ValueError('kernel path missing: '+name)
        info=subprocess.check_output([prefix+'readelf','-p','.modinfo',str(module)]);(out/'module.modinfo').write_bytes(info)
        if release.encode() not in info or not all(sha(p)==h for p,h in before.items()) or source_digest(inventory_source(root))!=anchor:raise ValueError('kernel inputs/source changed')
        rec.update(natural_exit=0,module_sha256=sha(module),module_bytes=len(data),source_sha256=anchor,cpu_sha256=sha(cpu),kernel_release=release,parent_inputs=before,parent_unchanged=True,full_bindings_in_elf=True)
    except Exception as exc:rec.update(natural_exit=1,error=str(exc))
    (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
    if rec['natural_exit']:raise ValueError(rec['error'])
    return rec

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--env',required=True);p.add_argument('--root',default='.');p.add_argument('--out',required=True);p.add_argument('--bound',required=True);a=p.parse_args()
    from scripts.demo.coordination import coordinated
    with coordinated(a.env,pathlib.Path(a.out).resolve().parent,'cross'):print(json.dumps(build(a.env,a.root,a.out,a.bound)))
