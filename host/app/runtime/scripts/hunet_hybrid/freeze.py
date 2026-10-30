"""Immutable v5 workload plus a fixed CPU role map; never hardware evidence."""
import argparse,hashlib,json,pathlib,re,struct
ROOT=pathlib.Path(__file__).resolve().parents[2]
BASE='9b3c4d58de551e49af45d3f567c91bf0311aa75c'
CPU=[(8,296,393216),(17,591,393216),(28,768,196608),(37,847,196608),
     (49,896,98304),(55,913,98304),(61,930,98304),(67,947,98304),
     (73,964,98304),(79,981,98304),(87,1000,49152),(93,1011,49152),(98,1020,1280)]
ROLES=['weights','jobs_file','aux_file','onnx','original_acceptance','original_R2','original_ORT_FP32','bit','xsa']
def original(root=ROOT):return json.loads((root/'contracts/hunet_v5.json').read_text(encoding='utf-8'))
def identities(m):return {role:next(x['sha256'] for x in m['files'] if x['role']==role) for role in ROLES}
def freeze_contract(root):
    root=pathlib.Path(root);m=original(root)
    rows=[]
    for line in (root/'generated/hunet/catalog.h').read_text().splitlines():
        if line.startswith('{0x'):rows.append([int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)u',line)])
    if len(rows)!=1023 or any(len(x)!=32 for x in rows):raise ValueError('JOB_CATALOG_LAYOUT')
    wire=b''.join(struct.pack('<32I',*x) for x in rows)
    if hashlib.sha256(wire).hexdigest()!=identities(m)['jobs_file']:raise ValueError('ORIGINAL_JOB_CATALOG_CHANGED')
    cpu=[dict(stage=x[30],sequence=x[4],elements=x[7]) for x in rows if x[3]==6]
    stages=(root/'generated/hunet/stages.h').read_text().count('u,0x')
    c=dict(schema=1,scope='FIXED_HYBRID_ROLE_CONTRACT_NOT_BOARD',source_base=BASE,program_id=8,
           cpu_algorithm='ERF_NATIVE_V1',threads=4,logical_jobs=len(rows),gpu_jobs=len(rows)-len(cpu),
           cpu_jobs=len(cpu),stages=stages,cpu_stages=cpu,maximum_cpu_bytes=max(x['elements'] for x in cpu)*4,
           profile=m['profile'],identities=identities(m))
    verify_baseline(c,m);return c
def verify_baseline(c,expected=None):
    # Historical releases supply their independently verified source model.
    m=original() if expected is None else expected
    if c.get('source_base')!=BASE or c.get('profile')!=m['profile'] or c.get('identities')!=identities(m):raise ValueError('BASELINE_IDENTITY_CHANGED')
    if [c.get(k) for k in ('program_id','threads','logical_jobs','gpu_jobs','cpu_jobs','stages','maximum_cpu_bytes')]!=[8,4,1023,1010,13,100,1572864]:raise ValueError('HYBRID_COUNTS_OR_BOUNDS')
    if c.get('cpu_algorithm')!='ERF_NATIVE_V1' or c.get('cpu_stages')!=[dict(stage=s,sequence=q,elements=n) for s,q,n in CPU]:raise ValueError('CPU_ROLE_MAP_CHANGED')
    return None
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--out',type=pathlib.Path,required=True);a=p.parse_args()
    with a.out.open('w',encoding='utf-8',newline='\n') as f:
        f.write(json.dumps(freeze_contract(ROOT),ensure_ascii=False,indent=2)+'\n')
    print('HYBRID_V5_BASELINE_ROLE_MAP_FROZEN_NOT_BOARD')
