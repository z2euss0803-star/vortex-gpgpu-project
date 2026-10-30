"""Actual partial component cross build. Never runs target ELF or builds an image."""
import argparse,hashlib,json,os,pathlib,re,shlex,shutil,subprocess,sys,tarfile,time
from scripts.demo.audit_cross import audit_cross
def sha(path):return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
def software_identity(root):
    rows=[]
    for folder in ('demo','mosaic','adapters','viewer','tools','include','core','generated/demo','config/demo','config/mosaic','data/demo','scripts'):
        for p in (root/folder).rglob('*'):
            if p.is_file() and '__pycache__' not in p.parts and p.suffix not in ('.pyc','.o','.ko'):rows.append((str(p.relative_to(root)),p.stat().st_size,sha(p)))
    for p in root.glob('Makefile*'):rows.append((p.name,p.stat().st_size,sha(p)))
    return hashlib.sha256(json.dumps(sorted(set(rows)),separators=(',',':')).encode()).hexdigest()
def build(env_path,root,out):
    env=json.loads(pathlib.Path(env_path).read_text());root=pathlib.Path(root).resolve();out=pathlib.Path(out).resolve();out.mkdir(parents=True,exist_ok=False)
    record=dict(scope='PARTIAL_AARCH64_COMPONENTS_NOT_SYSTEM',raw_gpu_cycles=None,elfs=[],commands=[],full_cross='NOT_COMPLETE_MOSAIC_NUMERIC_GATE_OPEN')
    prefix=env['aarch64_prefix'];deps=pathlib.Path(env['demo_deps']);before=software_identity(root);record['source_sha256']=before
    def run(cmd,cwd,extra_env=None):
        start=time.monotonic();p=subprocess.run(cmd,cwd=str(cwd),env=extra_env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        name='command-%02d.log'%len(record['commands']);(out/name).write_bytes(p.stdout);record['commands'].append(dict(argv=cmd,cwd=str(cwd),natural_exit=p.returncode,log=name,sha256=hashlib.sha256(p.stdout).hexdigest(),elapsed_s=time.monotonic()-start));(out/'build-receipt.json').write_text(json.dumps(record,indent=2)+'\n')
        if p.returncode:raise RuntimeError(name+' failed: '+p.stdout.decode('utf-8','replace')[-1500:])
        return p.stdout.decode()
    try:
        record['compiler_version']=subprocess.check_output([prefix+'gcc','--version']).decode();record['target']=subprocess.check_output([prefix+'gcc','-dumpmachine']).decode().strip();record['sysroot']=subprocess.check_output([prefix+'gcc','-print-sysroot']).decode().strip()
        if record['target']!='aarch64-xilinx-linux' or '9.2.0' not in record['compiler_version']:raise ValueError('matching 2020.1 SDK required')
        ranlib=pathlib.Path(prefix+'ranlib')
        if not ranlib.is_file():
            # The frozen SDK exports target ar but no ranlib wrapper. GNU ar s
            # builds the same archive index; never substitute a host archiver.
            ranlib=out/'target-ranlib'
            ranlib.write_text('#!/bin/sh\nexec '+shlex.quote(prefix+'ar')+' s "$@"\n');ranlib.chmod(0o755)
            record['ranlib_adapter']=dict(path=str(ranlib.relative_to(out)),sha256=sha(ranlib),target_ar=prefix+'ar')
        json_source=deps/'json-c-018-vxl1-build/source'
        if sha(json_source/'json_tokener.c')!='5cfe8ffcf4e8cb75aeda1d727d1728f9dcfef62f1bd0b3b9e702751dbfea0479':raise ValueError('exact strict UTF8 source patch required')
        if sha(deps/'json-c-0.18.tar.gz')!='876ab046479166b869afc6896d288183bbc0e5843f141200c677b3e8dfb11724':raise ValueError('json-c archive changed')
        jb=out/'json-build';jb.mkdir();jp=out/'json-target';native_env=dict(os.environ,LD_LIBRARY_PATH=str(deps/'native/usr/lib/x86_64-linux-gnu'))
        run([str(deps/'native/usr/bin/cmake'),str(json_source),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_SYSTEM_NAME=Linux','-DCMAKE_SYSTEM_PROCESSOR=aarch64','-DCMAKE_C_COMPILER='+prefix+'gcc','-DCMAKE_AR='+prefix+'ar','-DCMAKE_RANLIB='+str(ranlib),'-DBUILD_TESTING=OFF','-DBUILD_SHARED_LIBS=OFF','-DCMAKE_INSTALL_PREFIX='+str(jp)],jb,native_env)
        run(['make','-j1'],jb,native_env);run(['make','install'],jb,native_env)
        archive=deps/'libdrm-2.4.100.tar.gz'
        if sha(archive)!='6a5337c054c0c47bc16607a21efa2b622e08030be4101ef4a241c5eb05b6619b':raise ValueError('libdrm archive changed')
        unpack=out/'drm-source';unpack.mkdir()
        with tarfile.open(str(archive)) as tar:
            members={m.name:m for m in tar}
            for name,m in members.items():
                path=pathlib.PurePosixPath(name)
                if path.is_absolute() or '..' in path.parts:raise ValueError('unsafe upstream member')
                if m.isdir():continue
                if m.issym() or m.islnk():raise ValueError('upstream links require explicit audit')
                if not m.isfile():raise ValueError('upstream special member')
                target=unpack.joinpath(*path.parts);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(tar.extractfile(m).read());target.chmod(m.mode&0o777)
                # Preserve release timestamps so distributed configure/Makefile.in
                # are used rather than accidentally invoking maintainer autogen.
                os.utime(str(target),(m.mtime,m.mtime))
        ds=unpack/'libdrm-2.4.100';db=out/'drm-build';db.mkdir();dp=out/'drm-target'
        configure=ds/'configure';help_text=subprocess.check_output(['sh',str(configure),'--help']).decode()
        flags=['--disable-shared','--enable-static','--disable-libkms','--disable-intel','--disable-radeon','--disable-amdgpu','--disable-nouveau','--disable-vmwgfx','--disable-udev','--disable-cairo-tests','--disable-manpages','--disable-valgrind']
        for feature in ('omap-experimental-api','exynos-experimental-api','freedreno','tegra-experimental-api','etnaviv-experimental-api','vc4','install-test-programs'):
            if '--enable-'+feature in help_text or '--disable-'+feature in help_text:flags.append('--disable-'+feature)
        target_env=dict(os.environ,CC=prefix+'gcc',AR=prefix+'ar',RANLIB=str(ranlib),NM=prefix+'nm',STRIP=prefix+'strip',PKG_CONFIG_LIBDIR='/nonexistent-vxl-no-host-pkgconfig')
        run(['sh',str(configure),'--host=aarch64-xilinx-linux','--build=x86_64-pc-linux-gnu','--prefix='+str(dp)]+flags,db,target_env)
        run(['make','-j1'],db,target_env);run(['make','install'],db,target_env)
        objects=out/'objects'
        run(['make','-f','Makefile.demo','-j1','OUT='+str(objects),'CC='+prefix+'gcc','CXX='+prefix+'g++','DEMO_JSON_PREFIX='+str(jp),'DEMO_DRM_PREFIX='+str(dp),str(objects/'demo/vxl_demo'),str(objects/'demo/vxl_dp_viewer'),str(objects/'demo/historical_preview')],root)
        needed=set();required_versions=set()
        for name in ('vxl_demo','vxl_dp_viewer','historical_preview'):
            p=objects/'demo'/name;record['elfs'].append(dict(path=str(p.relative_to(out)),bytes=p.stat().st_size,sha256=sha(p)))
            inspection=subprocess.check_output([prefix+'readelf','-hW','-lW','-dW','-VW',str(p)]).decode();(out/(name+'.readelf')).write_text(inspection)
            needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',inspection))
            required_versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_)[0-9.]+)',inspection))
        # Legacy SDK helpers use sibling absolute imports. Bind that exact
        # frozen directory explicitly; software_identity includes scripts/.
        sys.path.insert(0,str(root/'scripts/hunet'))
        try:
            from cross_check import runtime_inventory
        finally:
            sys.path.pop(0)
        runtime=out/'runtime';runtime.mkdir();runtime_files,available_versions=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf')
        if not required_versions<=set(available_versions):raise ValueError('target rootfs symbol versions unavailable: '+repr(sorted(required_versions-set(available_versions))))
        record['runtime']=dict(needed=sorted(needed),required_versions=sorted(required_versions),files=runtime_files,rootfs_sha256=sha(env['rootfs']))
        record['dependencies']={str(p.relative_to(out)):dict(bytes=p.stat().st_size,sha256=sha(p)) for p in (jp/'lib/libjson-c.a',dp/'lib/libdrm.a')}
        record['source_after_sha256']=software_identity(root)
        if before!=record['source_after_sha256']:raise ValueError('source changed during component build')
        record['natural_exit']=0;(out/'cross-manifest.json').write_text(json.dumps(record,indent=2)+'\n');record['audit']=audit_cross(out/'cross-manifest.json')
    except Exception as exc:
        record['natural_exit']=1;record['error']=str(exc);(out/'build-receipt.json').write_text(json.dumps(record,indent=2)+'\n');raise
    (out/'build-receipt.json').write_text(json.dumps(record,indent=2)+'\n');return record

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--env',required=True);p.add_argument('--out',required=True);p.add_argument('--root',default='.');p.add_argument('--bound',action='store_true');a=p.parse_args()
    if a.bound:
        from scripts.demo.build_bound import build as bound_build
        from scripts.demo.coordination import coordinated
        with coordinated(a.env,pathlib.Path(a.out).resolve().parent,'cross'):r=bound_build(a.env,a.root,a.out)
        print(json.dumps(dict(scope=r['scope'],natural_exit=r['natural_exit'],cpu_sha256=r['cpu_sha256'])))
    else:
        r=build(a.env,a.root,a.out);print(json.dumps(dict(scope=r['scope'],audit=r['audit'],natural_exit=r['natural_exit'])))
