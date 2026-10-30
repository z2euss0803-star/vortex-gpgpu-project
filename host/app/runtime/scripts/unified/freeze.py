"""Freeze expected identities, refusing implicit imports of mutable source."""
import pathlib,hashlib,json,re

def freeze_unified(inputs, out):
    seen=set();rows=[]
    for row in inputs:
        if not isinstance(row,dict) or not re.fullmatch('[0-9a-f]{64}',row.get('sha256','')):raise ValueError('expected SHA required')
        p=pathlib.Path(row['path'])
        if p.is_symlink() or not p.is_file():raise ValueError('regular immutable input required')
        key=str(p.resolve())
        if key in seen:raise ValueError('duplicate input')
        seen.add(key);h=hashlib.sha256();n=0
        with p.open('rb') as f:
            for b in iter(lambda:f.read(1048576),b''):h.update(b);n+=len(b)
        if h.hexdigest()!=row['sha256']:raise ValueError('input identity changed: '+key)
        rows.append({'path':key,'bytes':n,'sha256':h.hexdigest(),'role':row['role']})
    result={'version':1,'files':sorted(rows,key=lambda x:x['path'])};out=pathlib.Path(out)
    if out.exists():
        if json.loads(out.read_text(encoding='utf-8'))!=result:raise ValueError('immutable lock changed')
    else:
        out.parent.mkdir(parents=True,exist_ok=True)
        with out.open('x',encoding='utf-8',newline='\n') as f:json.dump(result,f,ensure_ascii=False,indent=2);f.write('\n')
    return result

def validate_task_map(rows):
    expected={(line,i) for line,n in [('NN',24),('IMAGE',20),('P8',18)] for i in range(1,n+1)};seen=set()
    for row in rows:
        key=(row['line'],row['task'])
        if key in seen or key not in expected:raise ValueError('duplicate or unknown task')
        seen.add(key)
        if not row.get('mapped_to') or any(not re.fullmatch('U(?:0[1-9]|1[0-9]|2[0-5])',x) for x in row['mapped_to']):raise ValueError('invalid target task')
        if row.get('status')=='INHERITED_PASS':
            if not re.fullmatch('[0-9a-f]{40}',row.get('commit','')) or not row.get('receipt') or row.get('scope') not in ('HOST_IDENTITY_ONLY','HOST_CORE_ONLY','SOURCE_ONLY'):raise ValueError('inherited scope/receipt required')
        elif row.get('status') not in ('PENDING','REUSED_SOURCE_REVALIDATE_BOUNDARY'):raise ValueError('unknown task status')
    if seen!=expected:raise ValueError('incomplete original task map')
    return rows
