"""Identity-bounded sequential host propagation; no TCU, A53 or board claim."""
import csv,hashlib,json,struct,subprocess
from functools import lru_cache
from pathlib import Path
from scripts.hunet_opt.catalog import validate_catalog,canonical_bytes
ROOT=Path(__file__).resolve().parents[2]
def checked_asset(root,record):
    name=record.get('path','');root=Path(root).resolve()
    if not name or Path(name).is_absolute() or '\\' in name or any(p in ('','..','.') for p in name.split('/')):raise ValueError('ASSET_PATH')
    p=root
    for component in name.split('/'):
        p=p/component
        if p.is_symlink():raise ValueError('ASSET_SYMLINK')
    if not p.is_file():raise ValueError('ASSET_MISSING')
    b=p.read_bytes()
    if len(b)!=record.get('bytes') or hashlib.sha256(b).hexdigest()!=record.get('sha256'):raise ValueError('ASSET_IDENTITY')
    if record.get('dtype')=='FP32':
        shape=record.get('shape');n=1
        if not isinstance(shape,list) or not shape:raise ValueError('SHAPE_REQUIRED')
        for d in shape:
            if type(d)!=int or d<=0:raise ValueError('SHAPE')
            n*=d
        if n*4!=len(b):raise ValueError('SHAPE_SIZE')
    return p,b
@lru_cache(maxsize=1)
def stage_shapes():
    from scripts.hunet_opt.graph import frozen
    jobs=frozen()[0]['jobs'];stages=json.loads((ROOT/'generated/hunet/catalog.json').read_bytes())['stages']
    return [[jobs[s['last_sequence']-1][k] for k in (17,20,21)] for s in stages]
def validate_stages(stages):
    frozen=json.loads((ROOT/'generated/hunet/catalog.json').read_bytes())['stages']
    if len(stages)!=100:raise ValueError('FULL_STAGE_COVERAGE')
    for i,(r,s) in enumerate(zip(stages,frozen)):
        if r.get('stage')!=i or r.get('bytes')!=s['bytes'] or r.get('gpu')!=s['gpu'] or r.get('shape')!=stage_shapes()[i] or r.get('dtype')!='FP32':raise ValueError('STAGE_IDENTITY')
def run_reference(catalog,assets,out):
    validate_catalog(catalog);assets=Path(assets);out=Path(out);out.mkdir(exist_ok=False)
    spec=json.loads((assets/'reference-assets.json').read_bytes())
    inp,ib=checked_asset(assets,spec['input']);weights,wb=checked_asset(assets,spec['weights']);engine,eb=checked_asset(assets,spec['engine'])
    frozen_input=json.loads((ROOT/'contracts/hunet_inputs.json').read_bytes())['records'][0]
    contract=json.loads((ROOT/'contracts/hunet_v5.json').read_bytes())
    if len(ib)!=196608 or spec['input']['sha256']!=frozen_input['sha256'] or spec['input'].get('shape')!=[3,128,128] or spec['input'].get('dtype')!='FP32':raise ValueError('INPUT_IDENTITY')
    if len(wb)!=23685056 or not any(f['bytes']==len(wb) and f['sha256']==spec['weights']['sha256'] for f in contract['files']):raise ValueError('WEIGHTS_IDENTITY')
    if spec.get('scope')!='HOST_REFERENCE_ASSETS' or not spec.get('engine_source_sha256'):raise ValueError('ENGINE_PROVENANCE')
    source=ROOT/'tests/hunet_opt/reference_engine.cpp'
    if hashlib.sha256(source.read_bytes()).hexdigest()!=spec['engine_source_sha256']:raise ValueError('ENGINE_SOURCE_IDENTITY')
    native=out/'native';native.mkdir()
    command=[str(engine),str(inp),str(weights),str(native),'parallel' if catalog['layout']['execution_policy']['parallel_gn'] else 'ordered']
    with (out/'stdout.log').open('xb') as a,(out/'stderr.log').open('xb') as b:
        p=subprocess.run(command,stdout=a,stderr=b)
    receipt=dict(scope='HOST_NUMERIC_MODEL_NOT_TCU_NOT_GPU_NOT_A53',candidate=catalog['candidate'],command=command,natural_exit=p.returncode,assets=spec,gpu_cycles=None,board='NOT_RUN')
    (out/'receipt.json').write_bytes(canonical_bytes(receipt))
    if p.returncode:raise ValueError('REFERENCE_ENGINE_FAILED:'+str(p.returncode))
    stages=[];frozen=json.loads((ROOT/'generated/hunet/catalog.json').read_bytes())['stages']
    for i,s in enumerate(frozen):
        b=(native/('%03d.f32'%i)).read_bytes()
        if len(b)!=s['bytes'] or any(not __import__('math').isfinite(v[0]) for v in struct.iter_unpack('<f',b)):raise ValueError('STAGE_OUTPUT')
        producers=[e for e in catalog['entries'] if e['first_stage']<=i<=e['last_stage']]
        stages.append(dict(stage=i,bytes=len(b),gpu=s['gpu'],shape=stage_shapes()[i],dtype='FP32',sha256=hashlib.sha256(b).hexdigest(),materialized=not any(i in e['virtual_stages'] for e in producers),scope='HOST_NUMERIC_MODEL'))
    validate_stages(stages);receipt['stages']=stages
    receipt['gn_stats']=list(csv.DictReader((native/'gn-stats.csv').open()))
    receipt['logits_sha256']=hashlib.sha256((native/'logits.f32').read_bytes()).hexdigest()
    (out/'propagation.json').write_bytes(canonical_bytes(receipt));return receipt
