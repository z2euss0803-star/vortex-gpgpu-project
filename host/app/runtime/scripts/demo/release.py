"""Package source, receipts and actual component binaries as explicitly partial."""
import argparse,hashlib,json,pathlib,zipfile
from scripts.demo.verify_release import safe_name,validate_states,verify_release
def release_files(files, states, out):
    validate_states(states)
    if not files:raise ValueError('empty software release')
    for name,path in files.items():
        safe_name(name)
        if not pathlib.Path(path).is_file() or pathlib.Path(path).is_symlink():raise ValueError('missing/symlink source')
    out=pathlib.Path(out);out.mkdir(parents=True,exist_ok=False);rows=[]
    for name,path in sorted(files.items()):
        data=pathlib.Path(path).read_bytes();target=out/'files'/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        digest=hashlib.sha256(data).hexdigest()
        if hashlib.sha256(target.read_bytes()).hexdigest()!=digest:raise ValueError('copy identity differs')
        rows.append(dict(path=name,bytes=len(data),sha256=digest))
    manifest=dict(schema=1,scope='PARTIAL_SOFTWARE_NOT_SYSTEM_NOT_READY_FOR_SD',states=states,files=rows)
    raw=(json.dumps(manifest,sort_keys=True,indent=2,ensure_ascii=False)+'\n').encode('utf-8');(out/'DELIVERY.json').write_bytes(raw)
    archive=out/'software-partial.zip'
    with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('DELIVERY.json',raw)
        for r in rows:z.write(out/'files'/r['path'],'files/'+r['path'])
    digest=hashlib.sha256(archive.read_bytes()).hexdigest();(out/'software-partial.zip.sha256').write_text(digest+'  software-partial.zip\n')
    return verify_release(out)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--out',required=True);p.add_argument('--status',required=True);a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[2]
    states=json.loads(pathlib.Path(a.status).read_text());files={}
    for folder in ('core','driver','include','userspace','runtime','tools','device','scripts','contracts','generated','vendor','tests','mock','petalinux','demo','mosaic','adapters','viewer','config/demo','data/demo','docs/demo','evidence/demo'):
        for f in (root/folder).rglob('*'):
            if f.is_file() and '__pycache__' not in f.parts and f.suffix not in ('.pyc','.o','.ko'):files[str(f.relative_to(root)).replace('\\','/')]=f
    for f in root.glob('Makefile*'):files[f.name]=f
    print(json.dumps(release_files(files,states,a.out),sort_keys=True))
