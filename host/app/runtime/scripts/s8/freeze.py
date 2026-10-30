"""Source byte freeze; output and evidence never enter their own identity."""
from pathlib import Path,PurePosixPath
import hashlib,json,subprocess,tarfile,io
PARENT='89b207718bae6d4528ee7f146843f422d5c8744b'
TREES=('core','driver','include','userspace','runtime','tools','device','scripts','contracts','generated','vendor','tests','mock','petalinux')
MUST=('Makefile','Makefile.s8','core/engine.c','core/s8_result.c','core/s8_state.c','driver/vxl_s8.c','driver/vxl_s8_tracefs.c','include/uapi/vx_linux.h','include/vxl/s8_wire.h','device/s8/main.cpp','device/s8/negative.cpp','device/s8/compat.cpp','device/s8/phase.cpp','userspace/s8_client.c','tools/vxl_s8.cpp','scripts/s8/audit_run.py','contracts/s8/cases.json')
def validate_paths(root,paths):
    root=Path(root);seen=set()
    for name in paths:
        path=PurePosixPath(name)
        if not name or '\\' in name or ':' in name or path.is_absolute() or any(p in ('.','..','build','release','evidence','__pycache__','.git') for p in path.parts) or name in seen:raise ValueError('unsafe/duplicate frozen path')
        if path.parts[0] not in TREES and not (len(path.parts)==1 and name.startswith('Makefile')):raise ValueError('outside source policy')
        if 'vxl_s8_binding' in path.name or path.name=='vxl_s8_release.h':raise ValueError('self-hashed generated binding header')
        target=root.joinpath(*path.parts)
        if not target.is_file() or any(p.is_symlink() for p in [target]+list(target.parents) if p!=root.parent):raise ValueError('source symlink/missing')
        seen.add(name)
    if not set(MUST)<=seen:raise ValueError('missing production source')
    return sorted(seen)
def inventory(root):
    root=Path(root);paths=[p.name for p in root.glob('Makefile*') if p.is_file()]
    for tree in TREES:
        if (root/tree).is_dir():
            paths += [p.relative_to(root).as_posix() for p in (root/tree).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix not in ('.pyc','.o','.ko','.so')]
    paths=validate_paths(root,paths)
    return [dict(path=p,bytes=(root/p).stat().st_size,sha256=hashlib.sha256((root/p).read_bytes()).hexdigest()) for p in paths]
def check_identity(parent,ancestor,dirty):
    if parent!=PARENT or ancestor is not True or dirty:raise ValueError('wrong parent or dirty production source')
    return True
def source_digest(root):
    return hashlib.sha256(json.dumps(inventory(root),sort_keys=True,separators=(',',':')).encode()).hexdigest()
def freeze_source(root,out):
    root=Path(root);out=Path(out)
    def git(*args):return subprocess.check_output(['git','-C',str(root),*args]).decode('utf-8').strip()
    parent=git('rev-parse',PARENT+'^{commit}');ancestor=subprocess.call(['git','-C',str(root),'merge-base','--is-ancestor',PARENT,'HEAD'])==0
    dirty=git('status','--porcelain','--',*TREES,'Makefile','Makefile.s8').splitlines();check_identity(parent,ancestor,dirty)
    rows=inventory(root);canonical=json.dumps(rows,sort_keys=True,separators=(',',':')).encode();sha=hashlib.sha256(canonical).hexdigest()
    out.mkdir(parents=True,exist_ok=False);archive=out/'source.tar.gz'
    with tarfile.open(str(archive),'w:gz') as tar:
        for row in rows:
            data=(root/row['path']).read_bytes()
            if hashlib.sha256(data).hexdigest()!=row['sha256']:raise ValueError('source changed during freeze')
            info=tarfile.TarInfo(row['path']);info.size=len(data);info.mode=0o644;info.mtime=0;tar.addfile(info,io.BytesIO(data))
    result=dict(version=1,parent=PARENT,commit=git('rev-parse','HEAD'),source_sha256=sha,source_files=rows,archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),scope='SOURCE_IDENTITY_NOT_BUILD')
    (out/'source-manifest.json').write_text(json.dumps(result,sort_keys=True,indent=2)+'\n',encoding='utf-8');return result
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path('.'));p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    result=freeze_source(a.root,a.out)
    print(json.dumps({k:v for k,v in result.items() if k!='source_files'},sort_keys=True))
