"""Actual RV32 -> provider -> CPU SHA -> dual bindings -> kernel-header DAG.

Build-only API. The caller owns resource/coordination locks. Source is a private
frozen copy; only the explicitly excluded generated headers are written here.
"""
import argparse,hashlib,json,os,pathlib,re,shutil,struct,subprocess,sys,time
from scripts.demo.dual_binding import inventory_source,source_digest,rebind_hunet,pending_hunet_gate
from scripts.demo.mosaic_catalog import mutable_ranges,make_template,template_header,kernel_header
from scripts.demo.provider_header import provider_header,REQUIRED_FILES
from scripts.demo.audit_cross import validate_elf
from scripts.mosaic.binding import make_binding
from scripts.hunet.audit_program import elf_info
from scripts.s8.audit_rv32 import sections

def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def canonical(v):return json.dumps(v,sort_keys=True,separators=(',',':')).encode()

def formal_input(path):
    p=pathlib.Path(path);m=json.loads((p.parent/'manifest.json').read_text())
    if m.get('row')!=1 or m.get('shape')!=[1,3,960,1280] or p.stat().st_size!=14745600 or sha(p)!=m['files']['input.f32']['sha256']:raise ValueError('frozen full 1280x960 formal input required')
    return p

def checked_dependency(path,expected):
    p=pathlib.Path(path)
    if p.is_symlink() or not p.is_file() or sha(p)!=expected:raise ValueError('frozen dependency changed: '+str(p))
    return p

def build(env_path,root,out):
    e=json.loads(pathlib.Path(env_path).read_text());root=pathlib.Path(root).resolve();out=pathlib.Path(out).resolve();out.mkdir(parents=True,exist_ok=False)
    rec=dict(scope='ACTUAL_DUAL_BOUND_AARCH64_NOT_IMAGE_NOT_BOARD',natural_exit=1,commands=[],gpu_raw_cycles=None)
    def run(cmd,cwd=root):
        start=time.monotonic();q=subprocess.run([str(x) for x in cmd],cwd=str(cwd),stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        name='command-%03d.log'%len(rec['commands']);(out/name).write_bytes(q.stdout);rec['commands'].append(dict(argv=[str(x) for x in cmd],cwd=str(cwd),natural_exit=q.returncode,elapsed_s=time.monotonic()-start,log=name));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
        if q.returncode:raise ValueError(name+' failed: '+q.stdout.decode('utf8','replace')[-1000:])
        return q.stdout.decode('utf8','replace')
    try:
        anchor=source_digest(inventory_source(root));rec['source_sha256']=anchor
        commit=e['source_commit']
        if not re.fullmatch('[0-9a-f]{40}',commit):raise ValueError('actual frozen source commit required')
        rec['source_commit']=commit
        frozen=json.loads(pathlib.Path(e['source_manifest']).read_text())
        if frozen['commit']!=commit:raise ValueError('actual committed source manifest required')
        for name,row in frozen['files'].items():
            p=pathlib.PurePosixPath(name)
            if p.is_absolute() or '..' in p.parts or '\\' in name:raise ValueError('source manifest path')
            q=root.joinpath(*p.parts)
            if q.is_symlink() or not q.is_file() or q.stat().st_size!=row['bytes'] or sha(q)!=row['sha256']:raise ValueError('source archive content differs '+name)
        rec['source_manifest_sha256']=sha(e['source_manifest'])
        prefix=e['aarch64_prefix'];version=subprocess.check_output([prefix+'gcc','--version']).decode();target=subprocess.check_output([prefix+'gcc','-dumpmachine']).decode().strip()
        if target!='aarch64-xilinx-linux' or '9.2.0' not in version:raise ValueError('matching 2020.1 AArch64 SDK required')
        rec.update(compiler_version=version,target=target)
        jp=pathlib.Path(e['json_target']);dp=pathlib.Path(e['drm_target'])
        for name,p in (('json',jp/'lib/libjson-c.a'),('drm',dp/'lib/libdrm.a')):checked_dependency(p,e[name+'_archive_sha256'])
        from scripts.demo.build_rv32 import build as rvbuild
        rv=out/'rv32';rr=rvbuild(env_path,root,rv)
        rows=json.loads((rv/'catalog-layouts.json').read_text());assets=json.loads(pathlib.Path(e['mosaic_assets_manifest']).read_text())
        if sha(e['mosaic_assets'])!=assets['sha256']:raise ValueError('actual native INT16 asset manifest differs')
        raw,eh,loads,symbols=elf_info(rv/'kernel.elf');ss,_,_=sections(raw,eh)
        spans=mutable_ranges([dict(name=n,type=s[1],flags=s[2],address=s[3],bytes=s[5]) for n,s in ss.items()],rr['runtime'])
        deps=hashlib.sha256(canonical(rr['dependencies'])).hexdigest();template=make_template(rr,rows,assets,spans,sha(root/'config/mosaic/model-v1.json'),deps,anchor)
        (out/'template.json').write_bytes(canonical(template)+b'\n')
        hardware=json.loads(pathlib.Path(e['hardware_manifest']).read_text());hw=out/'selected-hardware.json';hw.write_bytes(canonical(hardware)+b'\n')
        for key in ('bit','xsa'):
            checked_dependency(e[key],e[key+'_sha256'])
            if not any(r.get('sha256')==e[key+'_sha256'] for r in hardware['files']):raise ValueError('selected hardware manifest conflict')
        config=pathlib.Path(e['kernel_build'])/'.config'
        ds=dict(template['digests'],software_source=anchor,rv32_compiler=hashlib.sha256(canonical(dict(version=rr['compiler_version'],dependencies=rr['dependencies']))).hexdigest(),runner='01'*32,kernel_config=sha(config),hardware_manifest=sha(hw),bit=e['bit_sha256'],xsa=e['xsa_sha256'])
        mosaic=bytearray(make_binding(ds));mosaic[320:352]=bytes(32)
        hn=pathlib.Path(e['frozen_rv32']).parent;hunet=bytearray(rebind_hunet((hn/'binding-fused.bin').read_bytes(),anchor,'01'*32));hunet[256:288]=bytes(32)
        paths={
          'mosaic/program12/code.bin':rv/'code.bin','mosaic/program12/norm-rodata.bin':rv/'norm-rodata.bin','mosaic/weights.ms8':pathlib.Path(e['mosaic_assets']),'mosaic/input.f32':formal_input(e['mosaic_formal_input']),
          'display/hunet-labels.bin':root/'data/demo/hunet-labels.bin','display/hunet-glyphs-u8.bin':root/'data/demo/hunet-glyphs-u8.bin','display/hunet-source-roi0.rgb':root/'data/demo/hunet-source-roi0.rgb','display/hunet-historical-logits.f32':root/'tests/demo/fixtures/hunet/logits.f32','display/hunet-historical-probe.json':root/'tests/demo/fixtures/hunet/probe-result.json','hunet/input-roi0.f32':root/'data/demo/hunet-input-roi0.f32'}
        for name in REQUIRED_FILES-set(paths):paths[name]=hn/name[len('hunet/'):]
        records={n:dict(bytes=p.stat().st_size,sha256=sha(p)) for n,p in paths.items()}
        display=json.loads((root/'config/demo/hunet-display-assets.json').read_text())
        sys.path.insert(0,str(root/'scripts/hunet'))
        try:from cross_check import runtime_inventory
        finally:sys.path.pop(0)
        pre=out/'libm-preflight';pre.mkdir();libs,_=runtime_inventory(pathlib.Path(e['rootfs']),{'libm.so.6'},pre,prefix+'readelf');libm=libs['libm.so.6']['sha256']
        if libm!=(hn/'cpu/libm.sha256').read_text().strip():raise ValueError('CPU erff libm identity conflict')
        gen=root/'generated/demo';gen.mkdir(exist_ok=True)
        (gen/'mosaic_release_template.h').write_text(template_header(template));(gen/'release_assets.h').write_text(provider_header(records,bytes(mosaic),bytes(hunet),display['classes']['sha256'],display['font']['sha256'],libm))
        cfiles=sorted(p.relative_to(root).as_posix() for p in (root/'core').glob('*.c'))+['userspace/mosaic_client.c','userspace/hunet_client.c','userspace/hunet_cpu_gelu.c','userspace/hunet_fused_client.c','mosaic/assets.c','demo/json.c','demo/store.c','demo/runner.c','demo/owner.c','demo/dispatch.c','demo/switch.c','viewer/frame.c','viewer/prepare.c','viewer/publish.c','viewer/scale.c','viewer/compose.c','viewer/glyph.c','viewer/hunet_frame.c','viewer/send.c']
        cppfiles=['adapters/dual_runtime.cpp','demo/release_bound.cpp','demo/release_io.cpp','tools/vxl_demo.cpp','adapters/mosaic_runner.cpp','adapters/hunet_runner.cpp','adapters/hunet_result.cpp','adapters/hunet_glyph.cpp','tools/hunet_fused_session.cpp','tools/hunet_hybrid_reference.cpp','tools/hunet_timing.cpp']
        objects=[]
        for i,name in enumerate(cfiles+cppfiles):
            obj=out/('object-%03d.o'%i);cpp=name.endswith('.cpp');flags=['-std='+('c++17' if cpp else 'c11'),'-O2','-g','-Wall','-Wextra','-Werror','-fPIC','-fno-fast-math','-ffp-contract=off','-Iinclude','-I'+str(jp/'include'),'-Itools','-Igenerated']
            if name in ('userspace/hunet_client.c','userspace/hunet_fused_client.c'):flags+=['-D_GNU_SOURCE']
            run([prefix+('g++' if cpp else 'gcc')]+flags+['-c',name,'-o',obj]);objects.append(obj)
        release=out/'release';(release/'bin').mkdir(parents=True);cpu=release/'bin/vxl_demo'
        run([prefix+'g++']+objects+[jp/'lib/libjson-c.a','-lm','-pthread','-ldl','-Wl,-z,defs','-o',cpu]);validate_elf(cpu.read_bytes(),183)
        # The compiled provider's deferred slot is now filled in external bindings.
        mosaic[320:352]=bytes.fromhex(sha(cpu));hunet[256:288]=bytes.fromhex(sha(cpu));mosaic,hunet=bytes(mosaic),bytes(hunet)
        for name,p in paths.items():
            dest=release/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(str(p),str(dest));dest.chmod(0o644)
            if sha(dest)!=records[name]['sha256']:raise ValueError('payload changed while packaging')
        (release/'mosaic/binding.bin').write_bytes(mosaic);(release/'hunet/binding-fused.bin').write_bytes(hunet);(release/'hunet/numeric-gate.bin').write_bytes(pending_hunet_gate(hunet))
        (release/'hunet/cpu').mkdir();shutil.copyfile(str(cpu),str(release/'hunet/cpu/gelu.elf'));(release/'hunet/cpu/gelu.elf').chmod(0o644)
        # Existing viewer uses the same matching rootfs, static json-c and libdrm.
        viewer_out=out/'viewer-build';run(['make','-j1','OUT='+str(viewer_out),'CC='+prefix+'gcc','CXX='+prefix+'g++','DEMO_JSON_PREFIX='+str(jp),'DEMO_DRM_PREFIX='+str(dp),str(viewer_out/'demo/vxl_dp_viewer')])
        shutil.copyfile(str(viewer_out/'demo/vxl_dp_viewer'),str(release/'bin/vxl_dp_viewer'))
        shutil.copyfile(str(root/'petalinux/demo/demo-viewer-init'),str(release/'bin/demo-viewer-init'));(release/'bin/demo-viewer-init').chmod(0o755)
        needed=set();required=set()
        for name in ('vxl_demo','vxl_dp_viewer'):
            p=release/'bin'/name;p.chmod(0o755);validate_elf(p.read_bytes(),183)
            inspect=subprocess.check_output([prefix+'readelf','-hW','-lW','-dW','-VW',str(p)]).decode();(out/(name+'.readelf')).write_text(inspect)
            needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',inspect));required.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_)[0-9.]+)',inspect))
        runtime=out/'runtime';runtime.mkdir();libraries,versions=runtime_inventory(pathlib.Path(e['rootfs']),needed,runtime,prefix+'readelf')
        if not required<=set(versions):raise ValueError('actual rootfs lacks required symbol versions')
        headers=out/'kernel-headers';headers.mkdir()
        (headers/'vxl_mosaic_release.h').write_text(kernel_header(template,mosaic,(rv/'code.bin').read_bytes(),(rv/'norm-rodata.bin').read_bytes()))
        prog=json.loads((hn/'program9/program.json').read_text());raw=struct.pack('<2I',prog['wire_bytes'],prog['immutable_spans'][0]['bytes'])+bytes.fromhex(prog['wire_sha256'])+bytes.fromhex(prog['immutable_spans'][0]['sha256'])+hunet+hashlib.sha256(hunet).digest()
        if len(raw)!=616:raise ValueError('fused release extent')
        (headers/'vxl_fused_release.h').write_text('#ifndef VXL_FUSED_RELEASE_H\n#define VXL_FUSED_RELEASE_H\nstatic const union {struct vxl_fused_release value;vxl_u8 raw[616];} vxl_fused_release_union __attribute__((used))={.raw={'+','.join(str(x) for x in raw)+'}};\n#define vxl_fused_release_bound (vxl_fused_release_union.value)\n#endif\n')
        old=(hn/'build/vxl_hybrid_release.h').read_text();match=re.search(r'vxl_hybrid_binding_bytes\[384\].*?=\{([^}]+)\}',old,re.S)
        if not match:raise ValueError('old hybrid binding missing')
        hybrid=bytearray(int(x.strip(),0) for x in match.group(1).split(','))
        if len(hybrid)!=384:raise ValueError('old hybrid extent')
        hybrid[96:128]=bytes.fromhex(anchor)
        (headers/'vxl_hybrid_release.h').write_text('#ifndef VXL_HYBRID_RELEASE_H\n#define VXL_HYBRID_RELEASE_H\nstatic const vxl_u8 vxl_hybrid_binding_bytes[384] __attribute__((used))={'+','.join(str(x) for x in hybrid)+'};\n#endif\n')
        (headers/'vxl_hunet_source.h').write_text('#ifndef VXL_HUNET_SOURCE_H\n#define VXL_HUNET_SOURCE_H\nstatic const volatile vxl_u8 vxl_hunet_source_sha[32] __attribute__((used))={'+','.join(str(x) for x in bytes.fromhex(anchor))+'};\nstatic const char vxl_hunet_source_commit[] __attribute__((used))="'+commit+'";\n#endif\n')
        old_s8=checked_dependency(e['s8_header'],e['s8_header_sha256']);shutil.copyfile(str(old_s8),str(headers/'vxl_s8_release.h'))
        (out/'kernel-headers.json').write_text(json.dumps({p.name:sha(p) for p in headers.iterdir()},sort_keys=True,indent=2)+'\n')
        if source_digest(inventory_source(root))!=anchor:raise ValueError('source changed during bound build')
        files={p.relative_to(release).as_posix():dict(bytes=p.stat().st_size,sha256=sha(p)) for p in release.rglob('*') if p.is_file()}
        (out/'normalized-source.json').write_text(json.dumps(inventory_source(root),sort_keys=True,indent=2)+'\n')
        (release/'release-manifest.json').write_text(json.dumps(dict(scope='SOURCE_BOUND_BUILD_NOT_TARGET_NUMERIC_APPROVAL',source_sha256=anchor,source_commit=commit,files=files,gpu_raw_cycles=None),sort_keys=True,indent=2)+'\n')
        rec.update(natural_exit=0,cpu_sha256=sha(cpu),viewer_sha256=sha(release/'bin/vxl_dp_viewer'),mosaic_binding_sha256=hashlib.sha256(mosaic).hexdigest(),hunet_binding_sha256=hashlib.sha256(hunet).hexdigest(),runtime_files=libraries,required_versions=sorted(required),source_unchanged=True,formal_input_shape=[1,3,960,1280],hunet_measured_gate='PENDING_EXTERNAL_AUDIT',old_s8_header_sha256=sha(old_s8))
    except Exception as exc:rec.update(natural_exit=1,error=str(exc))
    (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
    if rec['natural_exit']:raise ValueError(rec['error'])
    return rec

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--env',required=True);p.add_argument('--root',default='.');p.add_argument('--out',required=True);a=p.parse_args()
    from scripts.demo.coordination import coordinated
    with coordinated(a.env,pathlib.Path(a.out).resolve().parent,'cross'):print(json.dumps(build(a.env,a.root,a.out)))
