"""Inspect real FIT/rootfs/DT/kernel/BOOT payloads; never runs the target."""
import argparse,gzip,hashlib,json,pathlib,re,subprocess,sys
from scripts.demo.build_kernel import checked_bindings
from scripts.demo.network import network_errors

def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()

def hardware_errors(env):
    return ['selected hardware drift '+name for name in ('bit','xsa') if sha(env[name])!=env[name+'_sha256']]
def payload_errors(files,expected):
    return ['payload '+n for n,r in expected.items() if n not in files or len(files[n])!=r['bytes'] or hashlib.sha256(files[n]).hexdigest()!=r['sha256']]
def load_errors(kernel_bytes,fit_bytes):
    return ['boot load overlap/limit'] if not 0<kernel_bytes<0x10000000-0x80000 or not 0<fit_bytes<0x20000000 else []
def library_errors(files,libraries):
    errors=[]
    for name,row in libraries.items():
        raw=files.get(row['path'],b'')
        if hashlib.sha256(raw).hexdigest()!=row['sha256'] or raw[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(raw[18:20],'little')!=183:errors.append('actual matching AArch64 library '+name)
    return errors

def audit(env_path,bound,project,out):
    e=json.loads(pathlib.Path(env_path).read_text());bound=pathlib.Path(bound);project=pathlib.Path(project);out=pathlib.Path(out);out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'hunet'))
    from scripts.hunet import image_audit as old
    image=project/'images/linux';release=bound/'release';result=dict(scope='ACTUAL_DEMO_IMAGE_CONTENT_NOT_DISPLAY_NOT_BOARD',integrity='FAIL',gpu_raw_cycles=None,errors=[])
    errors=result['errors']
    try:
        errors+=hardware_errors(e)
        cpu=release/'bin/vxl_demo';m,h=checked_bindings((release/'mosaic/binding.bin').read_bytes(),(release/'hunet/binding-fused.bin').read_bytes(),sha(cpu))
        kb=pathlib.Path(e['kernel_build']);native=kb.parent/'recipe-sysroot-native/usr/bin';fdt=str(native/'fdtget');dump=str(native/'dumpimage')
        for i,name in enumerate(('kernel.gz','dt.bin','rootfs.gz')):subprocess.check_call([dump,'-T','flat_dt','-p',str(i),'-o',str(out/name),str(image/'image.ub')],stdout=subprocess.DEVNULL)
        if gzip.decompress((out/'kernel.gz').read_bytes())!=(image/'Image').read_bytes() or (out/'dt.bin').read_bytes()!=(image/'system.dtb').read_bytes() or (out/'rootfs.gz').read_bytes()!=(image/'rootfs.cpio.gz').read_bytes():errors.append('FIT actual payload differs')
        for node,name in (('kernel@1','kernel.gz'),('fdt@system-top.dtb','dt.bin'),('ramdisk@1','rootfs.gz')):
            base='/images/'+node;children=subprocess.check_output([fdt,'-l',str(image/'image.ub'),base]).decode().split();checks=0
            for child in children:
                path=base+'/'+child
                if subprocess.check_output([fdt,'-t','s',str(image/'image.ub'),path,'algo']).decode().strip()=='sha256':
                    value=bytes(int(x,16) for x in subprocess.check_output([fdt,'-t','bx',str(image/'image.ub'),path,'value']).decode().split()).hex();checks+=1
                    if value!=sha(out/name):errors.append('FIT SHA '+node)
            if checks!=1:errors.append('FIT hash coverage '+node)
        props=old.dt_properties(image/'system.dtb',fdt);legacy_props=dict(props,compatible='vortex,hw4-hunet-v5');errors+=old.dt_errors(legacy_props)
        if props['compatible']!='vortex,hw4-s8-demo-v1':errors.append('DT dual profile factory missing')
        parser=old.legacy_cpio();cpio=gzip.decompress((out/'rootfs.gz').read_bytes());recipe=project/'components/yocto/layers/core/meta/classes/image_types.bbclass'
        errors+=parser.rootfs_payload_errors(cpio,image/'rootfs.tar.gz',sha(recipe));entries=parser.cpio_entries(cpio);files=old.resolved_regular_files(entries);errors+=old.ssh_entry_errors(entries)
        errors+=network_errors(files)
        manifest=json.loads((release/'release-manifest.json').read_text());expected={}
        for n,row in manifest['files'].items():expected['usr/'+n if n.startswith('bin/') else 'usr/share/codex-demo/'+n]=row
        errors+=payload_errors(files,expected)
        if files.get('usr/share/codex-demo/release-manifest.json')!=(release/'release-manifest.json').read_bytes():errors.append('rootfs release manifest')
        errors+=library_errors(files,json.loads((bound/'receipt.json').read_text())['runtime_files'])
        identity=json.loads((project/'demo-integration.json').read_text());expected_id=(identity['build_id']+'\n').encode()
        if files.get('etc/codex-build-id')!=expected_id:errors.append('build identity')
        config=(kb/'.config').read_text().splitlines()
        for value in ('CONFIG_VXL_LINUX=y','CONFIG_DRM=y','CONFIG_DRM_XLNX=y','CONFIG_DRM_ZYNQMP_DPSUB=y','CONFIG_DRM_GEM_CMA_HELPER=y','CONFIG_DMA_CMA=y','CONFIG_CMA_SIZE_MBYTES=256'):
            if value not in config:errors.append('actual config '+value)
        if sha(kb/'.config')!=m[352:384].hex():errors.append('built config changed; rebind needed')
        symbols=(kb/'System.map').read_text()
        for name in ('vxl_s8_attach','vxl_mosaic_attach','mosaic_program_register','vxl_fused_init','vxl_profile_setup'):
            if not re.search(r'^\S+\s+[Tt]\s+'+name+'$',symbols,re.M):errors.append('builtin kernel path '+name)
        for name in ('Image','vmlinux'):
            raw=(image/name).read_bytes()
            if m not in raw or h not in raw:errors.append('full compiled bindings '+name)
        actual=pathlib.Path(e['kernel_source'])/'drivers/misc/vxl'
        frozen=json.loads((bound/'normalized-source.json').read_text())
        for name,row in frozen.items():
            if name.split('/')[0] not in ('core','driver','include','generated') or name=='driver/Makefile':continue
            if not (actual/name).is_file() or sha(actual/name)!=row['sha256']:errors.append('actual kernel source '+name)
        header_manifest=json.loads((bound/'kernel-headers.json').read_text())
        for name,value in header_manifest.items():
            if not (actual/'driver'/name).is_file() or sha(actual/'driver'/name)!=value:errors.append('actual bound header '+name)
        makefile=(actual/'driver/Makefile').read_text()
        if 'obj-$(CONFIG_VXL_LINUX) += vxl_linux.o' not in makefile or '-I$(srctree)/drivers/misc/vxl/include -Werror' not in makefile:errors.append('actual builtin Makefile integration')
        errors+=old.boot_bit_errors((image/'BOOT.BIN').read_bytes(),old.bit_payload(pathlib.Path(e['bit']).read_bytes()))
        errors+=old.boot_script_errors((image/'boot.scr').read_bytes());errors+=load_errors((image/'Image').stat().st_size,(image/'image.ub').stat().st_size)
        result.update(files={n:dict(bytes=(image/n).stat().st_size,sha256=sha(image/n)) for n in ('BOOT.BIN','boot.scr','image.ub','Image','system.dtb','rootfs.tar.gz')},build_id=identity['build_id'],release_manifest_sha256=sha(release/'release-manifest.json'),cpu_sha256=sha(cpu),bit_sha256=sha(e['bit']),xsa_sha256=sha(e['xsa']),mosaic_binding_sha256=hashlib.sha256(m).hexdigest(),hunet_binding_sha256=hashlib.sha256(h).hexdigest(),source_sha256=manifest['source_sha256'],hunet_measured_gate='PENDING_EXTERNAL_AUDIT')
    except Exception as exc:errors.append(str(exc))
    result.update(integrity='FAIL' if errors else 'PASS',natural_exit=1 if errors else 0);(out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
    if errors:raise ValueError(str(errors))
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--env',required=True);p.add_argument('--bound',required=True);p.add_argument('--project',required=True);p.add_argument('--out',required=True);a=p.parse_args();print(json.dumps(audit(a.env,a.bound,a.project,a.out)))
