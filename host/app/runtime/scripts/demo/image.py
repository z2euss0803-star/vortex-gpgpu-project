"""Prepare/build an isolated matching PetaLinux2020.1 project; no media APIs."""
import argparse,datetime,hashlib,json,os,pathlib,re,shlex,shutil,signal,subprocess,tarfile,time
from scripts.demo.dual_binding import inventory_source,source_digest
from scripts.demo.resources import admit_resources
from scripts.demo.network import install_direct_network

def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def resource_facts(path):
    return dict(host=os.uname().nodename,account=subprocess.check_output(['id','-un']).decode().strip(),available_ram=int(next(x.split()[1] for x in pathlib.Path('/proc/meminfo').read_text().splitlines() if x.startswith('MemAvailable:')))*1024,free_disk=shutil.disk_usage(str(path)).free,other_heavy=[x for x in subprocess.check_output(['ps','-eo','comm']).decode().splitlines() if x.strip() in ('cc1','cc1plus','make','bitbake','bitbake-worker','vivado','ninja')],source_identity_verified=True,locks_owned=True)

def monitored(command,cwd,log,space_path):
    """Stop only this child process group on low disk; keep natural exit/log."""
    with pathlib.Path(log).open('xb') as stream:
        p=subprocess.Popen(command,cwd=str(cwd),stdout=stream,stderr=subprocess.STDOUT,start_new_session=True);low=False
        while p.poll() is None:
            if shutil.disk_usage(str(space_path)).free<2*1024**3:
                low=True;os.killpg(p.pid,signal.SIGTERM);break
            time.sleep(5)
        rc=p.wait()
    pathlib.Path(str(log)+'.exit').write_text(str(rc)+'\n')
    if low or rc:raise ValueError('LOW_SPACE_OWN_BUILD_STOPPED' if low else 'build command failed '+str(log))

def prepare(env_path,root,bound,project,out):
    root=pathlib.Path(root).resolve();bound=pathlib.Path(bound).resolve();project=pathlib.Path(project).resolve();out=pathlib.Path(out).resolve();out.mkdir(parents=True,exist_ok=False);e=json.loads(pathlib.Path(env_path).read_text());old=pathlib.Path(e['image_parent']).resolve()
    facts=resource_facts(old);gate=admit_resources(facts,'image');rec=dict(scope='ISOLATED_DEMO_IMAGE_PREPARATION_NOT_IMAGE',resources=gate,natural_exit=1,gpu_raw_cycles=None)
    try:
        b=json.loads((bound/'receipt.json').read_text());k=json.loads(pathlib.Path(e['bound_kernel_receipt']).read_text())
        from scripts.demo.audit_cross import audit_bound
        audit_bound(bound,pathlib.Path(e['bound_kernel_receipt']).parent,e['numeric_receipt'])
        if b['natural_exit'] or k['natural_exit'] or k['source_sha256']!=b['source_sha256'] or source_digest(inventory_source(root))!=b['source_sha256'] or k['cpu_sha256']!=b['cpu_sha256']:raise ValueError('matching completed source/CPU/kernel admission required')
        if project.exists() or project==old or project in old.parents or old in project.parents:raise ValueError('fresh disjoint project required')
        for key in ('bit','xsa'):
            if sha(e[key])!=e[key+'_sha256']:raise ValueError('selected hardware drift')
        project.mkdir()
        for item in old.iterdir():
            if item.is_file() and item.name not in ('s8-build.log','s8-build-receipt.json','s8-integration.json'):shutil.copy2(str(item),str(project/item.name))
            elif item.name=='.petalinux' and item.is_dir():shutil.copytree(str(item),str(project/item.name))
        shutil.copytree(str(old/'project-spec'),str(project/'project-spec'),symlinks=True)
        meta=project/'project-spec/meta-user';(project/'build').mkdir()
        install_direct_network(meta)
        for name in ('downloads','sstate-cache'):
            cache=(old/'build'/name).resolve()
            if not cache.is_dir():raise ValueError('matching shared cache missing')
            (project/'build'/name).symlink_to(cache,target_is_directory=True)
        def write(name,content):
            p=meta/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(content.encode() if isinstance(content,str) else content)
        # Exact source trees, with final new bindings and old S8 identity retained.
        packet=meta/'recipes-kernel/linux/files/vxl-source.tar.gz';packet.parent.mkdir(parents=True,exist_ok=True)
        with tarfile.open(str(packet),'w:gz',compresslevel=1) as t:
            for prefix in ('core','driver','include','generated'):
                for p in sorted((root/prefix).rglob('*')):
                    n=p.relative_to(root).as_posix()
                    if not p.is_file() or '__pycache__' in p.parts or p.suffix in ('.pyc','.o','.ko'):continue
                    override=bound/'kernel-headers'/p.name if prefix=='driver' else None
                    if override is not None and override.is_file():p=override
                    t.add(str(p),arcname=n,recursive=False)
            # Some release headers do not exist in the source baseline.
            for p in (bound/'kernel-headers').iterdir():
                if not (root/'driver'/p.name).exists():t.add(str(p),arcname='driver/'+p.name,recursive=False)
        # The existing tested built-in recipe copies this tree. The standalone
        # old S8 header is byte-identical to the preserved old ID binding.
        write('recipes-kernel/linux/files/vxl_s8_release.h',(bound/'kernel-headers/vxl_s8_release.h').read_bytes())
        release=bound/'release'
        for package,select in (('demo-runtime',lambda n:n.startswith('bin/')),('demo-data',lambda n:not n.startswith('bin/'))):
            recipe=root/'petalinux/demo'/(package+'.bb');write('recipes-apps/'+package+'/'+package+'.bb',recipe.read_bytes())
            dest=meta/'recipes-apps'/package/'files'/(package+'.tar.gz');dest.parent.mkdir(parents=True,exist_ok=True)
            with tarfile.open(str(dest),'w:gz',compresslevel=1) as t:
                for p in sorted(release.rglob('*')):
                    if p.is_file() and select(p.relative_to(release).as_posix()):t.add(str(p),arcname=p.relative_to(release).as_posix(),recursive=False)
        write('recipes-bsp/device-tree/files/system-user.dtsi',(root/'petalinux/demo/system-user-demo.dtsi').read_bytes())
        build_id='S8-MOSAIC-HUNET-DP-V1-'+datetime.datetime.utcnow().strftime('%Y%m%dT%H%M%SZ');write('recipes-apps/codex-firstboot/files/codex-build-id',build_id+'\n')
        for name in ('config','rootfs_config'):
            p=project/'project-spec/configs'/name
            if name=='rootfs_config':p.write_text(p.read_text()+'\nCONFIG_demo-runtime=y\nCONFIG_demo-data=y\n')
        p=meta/'conf/user-rootfsconfig';p.write_text(p.read_text()+'\nCONFIG_demo-runtime\nCONFIG_demo-data\n')
        with (meta/'conf/petalinuxbsp.conf').open('a') as f:f.write('\n# Isolated dual application cold image, serialized with C\nBB_NO_NETWORK = "1"\nBB_NUMBER_THREADS = "1"\nPARALLEL_MAKE = "-j1"\nBB_DISKMON_DIRS = "STOPTASKS,${TMPDIR},2200M,100K"\n')
        hw=project/'demo-hardware';hw.mkdir();shutil.copyfile(str(e['xsa']),str(hw/'system.xsa'))
        rec.update(natural_exit=0,build_id=build_id,source_sha256=b['source_sha256'],cpu_sha256=b['cpu_sha256'],bit_sha256=e['bit_sha256'],xsa_sha256=e['xsa_sha256'],parent=str(old),project=str(project),cma_budget=dict(configured_mib=256,maximum_two_dumb_buffers_mib=128,coherent_reserved_mib=1,physical_allocation='NOT_TESTED'),state='PREPARED_NOT_BUILT')
        (project/'demo-integration.json').write_text(json.dumps(rec,indent=2)+'\n')
    except Exception as exc:rec.update(natural_exit=1,error=str(exc))
    (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
    if rec['natural_exit']:raise ValueError(rec['error'])
    return rec

def build(env_path,project,out):
    e=json.loads(pathlib.Path(env_path).read_text());project=pathlib.Path(project).resolve();out=pathlib.Path(out).resolve();out.mkdir(parents=True,exist_ok=False)
    gate=admit_resources(resource_facts(project),'image');rec=dict(scope='ACTUAL_DEMO_IMAGE_BUILD_PENDING_CONTENT_AUDIT',resources=gate,natural_exit=1,gpu_raw_cycles=None)
    try:
        if json.loads((project/'demo-integration.json').read_text())['natural_exit']:raise ValueError('prepared candidate missing')
        def shell(args,name):
            command='set -e; . '+shlex.quote(e['petalinux_settings'])+'; '+' '.join(shlex.quote(str(x)) for x in args)
            monitored(['bash','-c',command],project,out/(name+'.log'),project)
        (project/'project-spec/hw-description').mkdir(exist_ok=True)
        shell(['petalinux-config','--get-hw-description='+str(project/'demo-hardware'),'--silentconfig'],'import')
        shell(['petalinux-build'],'build')
        im=project/'images/linux'
        shell(['petalinux-package','--boot','--fsbl',im/'zynqmp_fsbl.elf','--fpga',e['bit'],'--u-boot',im/'u-boot.elf','--pmufw',im/'pmufw.elf','--atf',im/'bl31.elf','--force','--output',im/'BOOT.BIN'],'package')
        rec.update(natural_exit=0,state='BUILT_PENDING_ACTUAL_CONTENT_AUDIT')
    except Exception as exc:rec.update(natural_exit=1,error=str(exc))
    (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
    if rec['natural_exit']:raise ValueError(rec['error'])
    return rec

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('kind',choices=('prepare','build'));p.add_argument('--env',required=True);p.add_argument('--root',default='.');p.add_argument('--bound');p.add_argument('--project',required=True);p.add_argument('--out',required=True);a=p.parse_args()
    from scripts.demo.coordination import coordinated
    with coordinated(a.env,pathlib.Path(a.out).resolve().parent,'image'):print(json.dumps(prepare(a.env,a.root,a.bound,a.project,a.out) if a.kind=='prepare' else build(a.env,a.project,a.out)))
