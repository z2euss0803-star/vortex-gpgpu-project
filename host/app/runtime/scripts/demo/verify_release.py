"""Partial offline package verification; never admits media or field actions."""
import argparse,hashlib,json,pathlib,zipfile
def validate_states(states):
    if not isinstance(states,dict) or states.get('raw_gpu_cycles') is not None:raise ValueError('no new GPU measurement exists')
    for name in ('BOARD_INT16','BOARD_HUNET','DP_PHYSICAL','PERFORMANCE'):
        if states.get(name)!='NOT_RUN':raise ValueError('offline package cannot promote '+name)
    if states.get('IMAGE')!='NOT_BUILT':raise ValueError('this partial release has no image admission')
    if not states.get('OFFLINE') or not states.get('CROSS'):raise ValueError('separate offline/cross states required')

def safe_name(name):
    p=pathlib.PurePosixPath(name)
    if not name or '\\' in name or ':' in name or p.is_absolute() or any(part in ('','..','.') for part in name.split('/')):raise ValueError('unsafe release path')
    return p

def verify_release(directory):
    root=pathlib.Path(directory);raw=(root/'DELIVERY.json').read_bytes();m=json.loads(raw)
    if m.get('schema')==2:
        from scripts.demo.system_release import verify_system_release
        return verify_system_release(directory)
    if m.get('schema')!=1 or m.get('scope')!='PARTIAL_SOFTWARE_NOT_SYSTEM_NOT_READY_FOR_SD':raise ValueError('partial scope required')
    validate_states(m['states'])
    seen=set();total=0
    for r in m['files']:
        name=r['path'];p=safe_name(name)
        if name in seen or type(r['bytes']) is not int or r['bytes']<0:raise ValueError('duplicate/invalid release member')
        seen.add(name);target=root/'files'/p
        if any(q.is_symlink() for q in [target]+list(target.parents) if q!=root.parent):raise ValueError('release symlink')
        data=target.read_bytes();total+=len(data)
        if len(data)!=r['bytes'] or hashlib.sha256(data).hexdigest()!=r['sha256'] or total>1024**3:raise ValueError('release file identity/size changed')
    archive=root/'software-partial.zip';digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    if (root/'software-partial.zip.sha256').read_text().split()[0]!=digest:raise ValueError('ZIP SHA changed')
    with zipfile.ZipFile(archive) as z:
        names=z.namelist()
        if len(names)!=len(set(names)) or set(names)!={'DELIVERY.json'}|{'files/'+n for n in seen}:raise ValueError('ZIP member set differs')
        if z.read('DELIVERY.json')!=raw:raise ValueError('ZIP manifest differs')
        for r in m['files']:
            info=z.getinfo('files/'+r['path'])
            if info.file_size!=r['bytes'] or hashlib.sha256(z.read(info)).hexdigest()!=r['sha256']:raise ValueError('ZIP member differs')
    return dict(state='VERIFIED_PARTIAL_SOFTWARE_NOT_READY_FOR_SD',files=len(seen),bytes=total,zip_sha256=digest,states=m['states'],raw_gpu_cycles=None)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--path',required=True);a=p.parse_args();print(json.dumps(verify_release(a.path),sort_keys=True))
