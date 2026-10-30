"""Immutable byte identities; safe to run on Python 3.6 on the build VM."""
import hashlib,json,pathlib

def freeze_inputs(inputs, out):
    rows=[];seen=set()
    for item in inputs:
        p=pathlib.Path(item)
        if p.is_symlink() or not p.is_file():raise ValueError('regular input required: '+str(p))
        canonical=str(p.resolve())
        if canonical in seen:raise ValueError('duplicate input: '+canonical)
        seen.add(canonical)
        h=hashlib.sha256();n=0
        with p.open('rb') as f:
            for b in iter(lambda:f.read(1048576),b''):h.update(b);n+=len(b)
        rows.append({'path':canonical,'bytes':n,'sha256':h.hexdigest()})
    result={'version':1,'files':sorted(rows,key=lambda x:x['path'])}
    out=pathlib.Path(out)
    if out.exists():
        if json.loads(out.read_text())!=result:raise ValueError('frozen input changed')
    else:
        out.parent.mkdir(parents=True,exist_ok=True)
        with out.open('x',encoding='utf-8',newline='\n') as f:json.dump(result,f,ensure_ascii=False,indent=2);f.write('\n')
    return result

def reserve_namespace(candidate, registries):
    if not isinstance(candidate.get('profile'),int) or candidate['profile']<=0:raise ValueError('invalid profile')
    for key,limit in [('programs',0xffffffff),('ioctl_nrs',255)]:
        values=candidate.get(key,[])
        if not values or len(set(values))!=len(values) or any(not isinstance(v,int) or v<0 or v>limit for v in values):raise ValueError('invalid '+key)
    for old in registries:
        if old.get('profile')==candidate['profile']:raise ValueError('profile already reserved')
        for key in ('programs','ioctl_nrs'):
            if set(candidate[key])&set(old.get(key,[])):raise ValueError(key+' already reserved')
    return candidate
