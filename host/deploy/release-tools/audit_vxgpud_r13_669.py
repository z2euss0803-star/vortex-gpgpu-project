import pathlib,sys,json,hashlib,subprocess,time,re,py_compile,shutil,argparse
sys.dont_write_bytecode=True
ROOT=pathlib.Path('/mnt/f/obsidian-codex/codex/work/s8_300s_optimization_20261026/evidence/a-drm-r4-20261027T073653Z/aarch64-helper')
OWNER=ROOT.parent
p=argparse.ArgumentParser();p.add_argument('--run',required=True);a=p.parse_args()
assert re.fullmatch('[a-z0-9][a-z0-9-]{0,48}',a.run)
TASK_G=pathlib.Path('/mnt/g/s8_software_r5_20261028/software-r13-performance-633').resolve();assert a.run.startswith('r5-');BUILD=TASK_G/('runtime-build-'+a.run)
OUT=TASK_G/('runtime-audit-'+a.run);OUT.mkdir(exist_ok=False)
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
rec=dict(scope='A_ACTUAL_INDEPENDENT_AARCH64_VXGPUD_COMPILE_PRODUCTION_SYMBOL_RUNTIME_CLOSURE_NOT_GPU_NOT_IMAGE_NOT_BOARD',commands=[],target_executed=False,gpu_raw_cycles=None,board='NOT_RUN_BY_THIS_SUBTASK',image='NOT_BUILT_BY_THIS_SUBTASK',installation=[])
def run(argv,name):
    so=OUT/(name+'.stdout.log');se=OUT/(name+'.stderr.log');start=time.monotonic()
    with so.open('xb') as o,se.open('xb') as e:r=subprocess.run(list(map(str,argv)),cwd=OUT,stdout=o,stderr=e)
    rec['commands'].append(dict(argv=list(map(str,argv)),cwd=str(OUT),natural_exit=r.returncode,elapsed_s=time.monotonic()-start,stdout=dict(path=str(so),sha256=sha(so)),stderr=dict(path=str(se),sha256=sha(se))))
    (OUT/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
    if r.returncode:raise RuntimeError(name+' exit='+str(r.returncode))
    return so.read_text(errors='replace')
def symbols(text):
    rows=[]
    for line in text.splitlines():
        m=re.match(r'^\s*\d+:\s+\S+\s+\S+\s+\S+\s+(\S+)\s+\S+\s+(\S+)\s+(\S+)',line)
        if not m:continue
        bind,ndx,name=m.groups()
        if bind=='LOCAL':continue
        parts=name.split('@');rows.append(dict(name=parts[0],version=parts[-1] if len(parts)>1 else None,binding=bind,undefined=ndx=='UND'))
    return rows
try:
    build=json.loads((BUILD/'receipt.json').read_text())
    assert build['natural_exit']==0 and build['execute_requested'] is True and build['target_executed'] is False
    assert [x['name'] for x in build['artifacts']]==['vxgpud']
    source=pathlib.Path(build['source_root']);lock=pathlib.Path(build['source_lock'])
    sys.path.insert(0,str(source))
    from scripts.unified.cross_common import verify_source_lock,provider
    from scripts.unified.audit_cross import inspect_elf,audit_production_symbols
    from scripts.unified.bind_kernel import require_cross_inputs
    before=verify_source_lock(source,lock,build['source_lock_sha256'])
    assert before==build['source_before']==build['source_after']
    rootfs_path=OWNER/'rootfs-runtime-02/receipt.json';rootfs=json.loads(rootfs_path.read_text());assert rootfs['natural_exit']==0
    rec.update(target='aarch64-s8-joint',executed_target_elfs=False,source_before=before,source_after=before,provider=provider(source,4),compiler_jobs=2)
    rec['commands']=list(build['commands'])
    rec['object_cache_reuse']=build.get('object_cache_reuse')
    invocations=[json.loads(line) for line in (BUILD/'compiler-invocations.jsonl').read_text().splitlines()]
    assert invocations and all(x['natural_exit']==0 for x in invocations)
    for row in invocations:
        if row['kind'] in ('gcc','g++'):
            assert row['output_verification']['fresh'] and row['output_verification']['aarch64_elf']
            assert row['source_snapshot_id']==before['source_snapshot_id']
    rec['compiler_invocations']=dict(path=str(BUILD/'compiler-invocations.jsonl'),sha256=sha(BUILD/'compiler-invocations.jsonl'),count=len(invocations),all_expected_outputs_verified=True)
    json_headers_path=OWNER/'json018-headers-01/headers-receipt-03.json';json_headers=json.loads(json_headers_path.read_text());assert json_headers['natural_exit']==0 and json_headers['library_rebuilt'] is False
    for h in json_headers['headers']:assert sha(h['path'])==h['sha256']
    rec['json_headers']=dict(receipt=str(json_headers_path),sha256=sha(json_headers_path),headers=json_headers['headers'],version='0.18',source_archive_sha256=json_headers['archive_sha256'])
    elf=pathlib.Path(build['artifacts'][0]['path']);digest=sha(elf);assert digest==build['artifacts'][0]['sha256']
    inspection=inspect_elf(elf,'aarch64',digest);audit_production_symbols(inspection)
    rec.update(build_receipt=dict(path=str(BUILD/'receipt.json'),sha256=sha(BUILD/'receipt.json')),source_lock=dict(path=str(lock),sha256=build['source_lock_sha256']),source_snapshot_id=before['source_snapshot_id'],runtime_source_id=before['runtime_source_id'],elf=inspection,production_symbols='PASS_EXISTING_UNCHANGED_AUDIT')
    readelf='/usr/bin/readelf'
    info=run([readelf,'-hW','-dW','-VW','-Ws',elf],'vxgpud-readelf')
    needed=set(inspection['needed']);required_versions=set(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',info))
    base=pathlib.Path('/mnt/g/xilinx/Vitis/2020.1/gnu/aarch64/nt/aarch64-linux/aarch64-xilinx-linux')
    frozen={
      'libc.so.6':('lib/libc-2.30.so','aeebc1b6491583b277e08ad1ca2509f4e28d628cb3df78073b71d676dea2bbe0'),
      'libm.so.6':('lib/libm-2.30.so','f5379ab0ba0797b5d66619425d8175263ac813ae7d7127376db2663be78a0a4b'),
      'libgcc_s.so.1':('lib/libgcc_s.so.1','46ecda5c0995aaf773c409100398418361cbebdadffa768a24c950a84ae7cf76'),
      'libstdc++.so.6':('usr/lib/libstdc++.so.6.0.27','48f2c5788373d1987b8c21920838a109e945b5054a239a18a2115d105331e3a0'),
      'libpthread.so.0':('lib/libpthread-2.30.so','edcd56cc1d6e01b0a2dec0258318b965835aacc9ae6ded0d1fbba5f1084caadb'),
      'ld-linux-aarch64.so.1':('lib/ld-2.30.so','2c307a67d7673dcb99e0e78bb6c3ae6ca468e21e5a0f7cbc6ad82846ad7afd4c')}
    assert needed<=set(frozen),sorted(needed-set(frozen))
    available_versions=set();exports=set();names=set();runtime=[]
    for i,(name,(rel,expected)) in enumerate(frozen.items()):
        sysroot_path=base/rel;actual=sha(sysroot_path);assert actual==expected,name
        path=pathlib.Path(rootfs['private_resolved_copies'][name]['path']);assert sha(path)==expected and rootfs['files'][name]['sha256']==expected,name
        text=run([readelf,'-hW','-dW','-VW','-Ws',path],'runtime-'+str(i))
        defs={x['name'] for x in symbols(text) if not x['undefined']}
        for x in symbols(text):
            if not x['undefined']:
                names.add(x['name'])
                if x['version']:exports.add((x['name'],x['version']))
        versions=set(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
        available_versions.update(versions);runtime.append(dict(name=name,path=str(path),rootfs_relative_path=rootfs['files'][name]['path'],sysroot_matching_path=str(sysroot_path),sha256=actual,versions=sorted(versions),exports=len(defs)))
    assert required_versions<=available_versions,sorted(required_versions-available_versions)
    undefined={(x['name'],x['version'],x['binding']) for x in symbols(info) if x['undefined']}
    missing=[];weak=[]
    for name,version,bind in sorted(undefined,key=str):
        present=(name,version) in exports if version else name in names
        if not present:
            (weak if bind=='WEAK' else missing).append(dict(name=name,version=version,binding=bind))
    assert not missing,missing
    rec['target_runtime']=dict(base_rootfs=rootfs['base_fit']['path'],base_image_sha256=rootfs['base_fit']['sha256'],rootfs_sha256=rootfs['rootfs_gzip']['sha256'],rootfs_container='SELECTED_FIT_RAMDISK_GZIP_CPIO',files=rootfs['files'],required_versions=sorted(required_versions),rootfs_resolution_receipt=dict(path=str(rootfs_path),sha256=sha(rootfs_path)),scope='ACTUAL_FROZEN_R3_FIT_RESOLVED_LIBRARY_INPUT_CLOSURE_NOT_NEW_IMAGE')
    rec['runtime_closure']=dict(needed=sorted(needed),required_versions=sorted(required_versions),runtime=runtime,strong_undefined_checked=len(undefined)-len(weak),missing_strong=missing,unresolved_weak=weak,scope='FROZEN_RUNTIME_FILE_SHA_VERSION_AND_DYNAMIC_EXPORT_CLOSURE_NOT_EXECUTION')
    dep=json.loads((OWNER/'dependency-identity.json').read_text());assert dep['dependency_id']==build['drm_dependency_id']
    filesystem_path=OWNER/'toolchain-static-01/libstdc++fs.a';filesystem_receipt=OWNER/'toolchain-static-01/receipt.json';fs=json.loads(filesystem_receipt.read_text());assert fs['natural_exit']==0 and sha(filesystem_path)==fs['library']['sha256']=='621178e119254ed7fe27001692d55b4e1564348462112042e4829998e96ab041'
    rec['static_dependencies']=dict(json=build['json_archive'],drm=dict(dependency_id=dep['dependency_id'],library=dep['library']),filesystem=dict(path=str(filesystem_path),bytes=filesystem_path.stat().st_size,sha256=sha(filesystem_path),receipt=dict(path=str(filesystem_receipt),sha256=sha(filesystem_receipt))))
    tools_base=pathlib.Path('/mnt/g/xilinx/Vitis/2020.1/gnu/aarch64/nt/aarch64-linux/bin')
    rec['tools']={name:dict(path=str(tools_base/('aarch64-linux-gnu-'+name+'.exe')),sha256=sha(tools_base/('aarch64-linux-gnu-'+name+'.exe'))) for name in ('gcc','g++','ld','as','ar','readelf')}
    version=run([tools_base/'aarch64-linux-gnu-g++.exe','--version'],'compiler-version');assert '9.2.0' in version
    machine=run([tools_base/'aarch64-linux-gnu-gcc.exe','-dumpmachine'],'compiler-machine').strip();assert machine=='aarch64-xilinx-linux'
    rec['compiler_sysroot']=run([tools_base/'aarch64-linux-gnu-gcc.exe','-print-sysroot'],'compiler-sysroot').strip()
    assert sha(build['json_archive']['path'])==build['json_archive']['sha256']
    assert sha(dep['library']['path'])==dep['library']['sha256']
    install=OUT/'installation';install.mkdir()
    binary=install/'vxgpud';shutil.copyfile(elf,binary);assert sha(binary)==digest
    client=source/'tools/vxgpuctl.py';py_compile.compile(str(client),cfile=str(OUT/'vxgpuctl.host-syntax-only.pyc'),doraise=True)
    copied=install/'vxgpuctl.py';shutil.copyfile(client,copied);assert sha(copied)==sha(client)
    rec['installation']=[dict(source=str(binary),destination='/usr/bin/vxgpud',mode='0755',sha256=digest),dict(source=str(copied),destination='/usr/bin/vxgpuctl',mode='0755',sha256=sha(copied),interpreter='python3',bytecode='HOST_SYNTAX_ONLY_NOT_INSTALLED')]
    assert sorted(p.name for p in install.iterdir())==['vxgpuctl.py','vxgpud']
    (OUT/'installation.json').write_text(json.dumps(rec['installation'],indent=2)+'\n')
    after=verify_source_lock(source,lock,build['source_lock_sha256']);assert before==after
    rec.update(source_after=after,source_after_id=after['source_snapshot_id'],natural_exit=0,elf_count=1,artifacts=[dict(path=str(elf),name='vxgpud',bytes=elf.stat().st_size,sha256=digest,elf=inspection,installable=True,scope='A_ACTUAL_PRODUCTION_COMPILE_NOT_EXECUTED')])
    require_cross_inputs(rec,after,'aarch64-s8-joint')
    rec['existing_cross_consumer_check']='PASS_REQUIRE_CROSS_INPUTS_UNCHANGED'
except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
finally:(OUT/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
print(json.dumps(dict(scope=rec['scope'],natural_exit=rec['natural_exit'],elf=rec['elf'],runtime_closure={k:v for k,v in rec['runtime_closure'].items() if k!='runtime'},installation=rec['installation'],receipt=str(OUT/'receipt.json'))))

