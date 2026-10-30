"""Full audited image delivery, kept separate from legacy partial packages.

Only local files/ZIPs are handled. This module cannot open media or a device.
"""
import hashlib,json,pathlib,re,shutil,zipfile
from scripts.demo.verify_release import safe_name
from scripts.demo.provider_header import REQUIRED_FILES
SCOPE='SOURCE_BOUND_IMAGE_AUDITED_NO_FIELD_PASS'
RUNTIME_FILES=REQUIRED_FILES|{'bin/vxl_demo','bin/vxl_dp_viewer','bin/demo-viewer-init','mosaic/binding.bin','hunet/binding-fused.bin','hunet/numeric-gate.bin','hunet/cpu/gelu.elf','release-manifest.json'}
REQUIRED={'runtime/'+n for n in RUNTIME_FILES}|{'images/BOOT.BIN','images/boot.scr','images/image.ub','source/source.tar.gz','source/source-manifest.json','source/hardware-manifest.json','hardware/top.bit','hardware/system.xsa','kernel/vxl_linux.ko','proof/cross-audit.json','proof/image-audit.json','proof/protected-failures.json'}

def sha(p):
    h=hashlib.sha256()
    with pathlib.Path(p).open('rb') as f:
        for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
    return h.hexdigest()

def runtime_errors(base,rows,manifest_sha):
    path=pathlib.Path(base)/'runtime/release-manifest.json';errors=[]
    if sha(path)!=manifest_sha:errors.append('runtime manifest differs from audited image/cross')
    manifest=json.loads(path.read_text())
    for name,row in manifest['files'].items():
        safe_name(name)
        if rows.get('runtime/'+name)!=row:errors.append('audited runtime payload '+name)
    if set(rows)&{'runtime/'+n for n in manifest['files']}!={'runtime/'+n for n in manifest['files']}:errors.append('runtime manifest membership')
    return errors

def validate_system_states(s):
    if not isinstance(s,dict) or s.get('raw_gpu_cycles') is not None:raise ValueError('no new offline GPU timing')
    for key,value in dict(OFFLINE='UBUNTU_SOFTWARE_PASS',CROSS='CROSS_PASS_NOT_BOARD',IMAGE='ACTUAL_DEMO_IMAGE_PASS_NOT_DISPLAY_NOT_BOARD',BOARD_INT16='NOT_RUN',BOARD_HUNET='NOT_RUN',DP_PHYSICAL='NOT_RUN',PERFORMANCE='NOT_RUN').items():
        if s.get(key)!=value:raise ValueError('separate audited image/field states required: '+key)

def proof_errors(base,rows):
    errors=[];base=pathlib.Path(base);image=json.loads((base/'proof/image-audit.json').read_text());cross=json.loads((base/'proof/cross-audit.json').read_text());source=json.loads((base/'source/source-manifest.json').read_text())
    # Current auditor uses this spelling; do not accept a generic/partial PASS.
    if image.get('scope')!='ACTUAL_DEMO_IMAGE_CONTENT_NOT_DISPLAY_NOT_BOARD':errors.append('actual image audit required')
    if image.get('integrity')!='PASS' or image.get('natural_exit')!=0 or image.get('errors'):errors.append('actual image audit failed')
    if cross.get('status')!='CROSS_PASS_NOT_BOARD' or cross.get('gpu_raw_cycles') is not None or image.get('gpu_raw_cycles') is not None or cross.get('source_sha256')!=image.get('source_sha256'):errors.append('matching cross audit required')
    errors+=runtime_errors(base,rows,image.get('release_manifest_sha256'))
    if {n[8:] for n in rows if n.startswith('runtime/')}!=RUNTIME_FILES:errors.append('fixed runtime membership differs')
    if image.get('release_manifest_sha256')!=cross.get('release_manifest_sha256'):errors.append('cross/image runtime manifest differs')
    if not re.fullmatch('[0-9a-f]{40}',source.get('commit','')) or rows['source/source.tar.gz']['sha256']!=source.get('archive_sha256'):errors.append('committed source archive identity')
    for n in ('BOOT.BIN','boot.scr','image.ub'):
        if rows['images/'+n]!=image.get('files',{}).get(n):errors.append('audited actual image bytes '+n)
    cpu=rows['runtime/bin/vxl_demo']['sha256']
    if cpu!=cross.get('cpu_sha256') or cpu!=image.get('cpu_sha256') or rows['kernel/vxl_linux.ko']['sha256']!=cross.get('module_sha256'):errors.append('actual CPU/kernel mismatch')
    for app,key in (('mosaic/binding.bin','mosaic_binding_sha256'),('hunet/binding-fused.bin','hunet_binding_sha256')):
        if rows['runtime/'+app]['sha256']!=image.get(key):errors.append('runtime binding '+app)
    for name,key in (('top.bit','bit_sha256'),('system.xsa','xsa_sha256')):
        if rows['hardware/'+name]['sha256']!=image.get(key):errors.append('selected hardware '+name)
    from scripts.demo.build_kernel import checked_bindings
    from scripts.demo.dual_binding import pending_hunet_gate
    m,h=checked_bindings((base/'runtime/mosaic/binding.bin').read_bytes(),(base/'runtime/hunet/binding-fused.bin').read_bytes(),cpu)
    if m[32:64].hex()!=cross.get('source_sha256') or h[96:128].hex()!=cross.get('source_sha256') or (base/'runtime/hunet/numeric-gate.bin').read_bytes()!=pending_hunet_gate(h):errors.append('source/gate identity')
    if rows['runtime/hunet/cpu/gelu.elf']['sha256']!=cpu:errors.append('same-executable HUNet CPU file')
    failures=json.loads((base/'proof/protected-failures.json').read_text())
    if failures.get('retained') is not True or not failures.get('archives'):errors.append('original failure preservation index missing')
    return errors

def verify_system_release(directory):
    root=pathlib.Path(directory);raw=(root/'DELIVERY.json').read_bytes();m=json.loads(raw)
    if m.get('schema')!=2 or m.get('scope')!=SCOPE:raise ValueError('full audited image scope required')
    validate_system_states(m['states']);rows={};total=0
    if not 1<=len(m['files'])<=5000:raise ValueError('bounded member count')
    for row in m['files']:
        name=row['path'];p=safe_name(name);target=root/'files'/p
        if name in rows or type(row['bytes'])!=int or row['bytes']<0 or not re.fullmatch('[0-9a-f]{64}',row['sha256']):raise ValueError('member schema')
        total+=row['bytes']
        if total>1024**3 or any(q.is_symlink() for q in [target]+list(target.parents) if q!=root.parent) or not target.is_file() or target.stat().st_size!=row['bytes'] or sha(target)!=row['sha256']:raise ValueError('member content/type changed')
        rows[name]=dict(bytes=row['bytes'],sha256=row['sha256'])
    if not REQUIRED<=set(rows):raise ValueError('full source/runtime/bit/image/proof set missing')
    errors=proof_errors(root/'files',rows)
    if errors:raise ValueError(str(errors))
    archive=root/'offline-demo-release.zip';digest=sha(archive)
    if (root/'offline-demo-release.zip.sha256').read_text().split()[0]!=digest:raise ValueError('ZIP identity')
    with zipfile.ZipFile(archive) as z:
        names=z.namelist()
        if len(set(names))!=len(names) or set(names)!={'DELIVERY.json'}|{'files/'+n for n in rows} or z.read('DELIVERY.json')!=raw:raise ValueError('ZIP member set/manifest')
        for n,row in rows.items():
            if z.getinfo('files/'+n).file_size!=row['bytes']:raise ValueError('ZIP member length')
            h=hashlib.sha256()
            with z.open('files/'+n) as f:
                for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
            if h.hexdigest()!=row['sha256']:raise ValueError('ZIP member content')
    return dict(state='VERIFIED_SOURCE_BOUND_IMAGE_NOT_BOARD',states=m['states'],files=len(rows),bytes=total,zip_sha256=digest,raw_gpu_cycles=None)

def release_system(files,states,out):
    validate_system_states(states)
    if not REQUIRED<=set(files):raise ValueError('full release proof inputs missing')
    out=pathlib.Path(out);out.mkdir(parents=True,exist_ok=False);rows=[];total=0
    for name,path in sorted(files.items()):
        p=safe_name(name);source=pathlib.Path(path)
        if source.is_symlink() or not source.is_file():raise ValueError('regular source required')
        total+=source.stat().st_size
        if total>1024**3:raise ValueError('delivery exceeds 1GiB explicit limit')
        target=out/'files'/p;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(str(source),str(target));h=sha(source)
        if sha(target)!=h:raise ValueError('copy changed')
        rows.append(dict(path=name,bytes=target.stat().st_size,sha256=h))
    errors=proof_errors(out/'files',{r['path']:dict(bytes=r['bytes'],sha256=r['sha256']) for r in rows})
    if errors:raise ValueError(str(errors))
    raw=(json.dumps(dict(schema=2,scope=SCOPE,states=states,files=rows),sort_keys=True,indent=2)+'\n').encode();(out/'DELIVERY.json').write_bytes(raw)
    archive=out/'offline-demo-release.zip'
    with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('DELIVERY.json',raw)
        for r in rows:z.write(str(out/'files'/r['path']),'files/'+r['path'])
    (out/'offline-demo-release.zip.sha256').write_text(sha(archive)+'  offline-demo-release.zip\n')
    return verify_system_release(out)
