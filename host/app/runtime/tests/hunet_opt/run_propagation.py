"""Real frozen assets, independent sequential host model, no board operations."""
import hashlib,importlib.util,json,struct,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT))
from scripts.hunet_opt.catalog import compile_catalog,canonical_bytes
from scripts.hunet_opt.reference import run_reference,checked_asset
from scripts.hunet_opt.numeric import compare_outputs
vault=ROOT.parent.parent;out=ROOT/'build'/('opt-propagation-'+time.strftime('%Y%m%dT%H%M%SZ',time.gmtime()));out.mkdir(exist_ok=False)
def rec(path,**extra):
    path=Path(path);b=path.read_bytes();return dict(path=path.relative_to(vault).as_posix(),bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),**extra)
contract=json.loads((ROOT/'contracts/hunet_v5.json').read_bytes());input_spec=json.loads((ROOT/'contracts/hunet_inputs.json').read_bytes())['records'][0]
weight_spec=next(f for f in contract['files'] if f['bytes']==23685056)
engine=ROOT/'build/opt_reference_engine'
assets=dict(scope='HOST_REFERENCE_ASSETS',input=rec(vault/input_spec['path'],dtype='FP32',shape=[3,128,128]),weights=rec(vault/weight_spec['path']),engine=rec(engine),engine_source_sha256=hashlib.sha256((ROOT/'tests/hunet_opt/reference_engine.cpp').read_bytes()).hexdigest())
# Manifest is private to this task's vault, with no global package installation.
(vault/'reference-assets.json').write_bytes(canonical_bytes(assets))
numeric_path=ROOT/'scripts/hunet/audit_run.py';spec=importlib.util.spec_from_file_location('propagation_old_numeric',numeric_path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
refs={}
for key,part in [('r2','R2_tree4_logits.npy'),('ort','R0_ort_logits.npy')]:
    record=next(f for f in contract['files'] if f['path'].endswith(part));p,b=checked_asset(vault,record);refs[key]=struct.pack('<8105f',*m.read_npy_row(p,0))
label=next(f for f in contract['files'] if f['path'].endswith('/GF_class_indices.json'));_,refs['labels']=checked_asset(vault,label)
history=ROOT/'tests/hunet_opt/fixtures/historical-39fb432d-logits.f32';historical=history.read_bytes()
assert hashlib.sha256(historical).hexdigest()=='58a75990aad6711810065fa7a3c83e81a3eaaba13769f0395eac95dd7fa48654'
review=compare_outputs(historical,refs,historical)
assert review['r2']['status']=='PASS' and review['ort']['status']=='FAIL' and review['ort']['mismatch_count']==8105 and review['history']['byte_identical']
(out/'historical-reaudit.json').write_bytes(canonical_bytes(review))
reports={}
for candidate in ('C1_T','G1'):
    r=run_reference(compile_catalog(candidate),vault,out/candidate)
    logits=(out/candidate/'native/logits.f32').read_bytes();comparison=compare_outputs(logits,refs,historical)
    (out/candidate/'numeric.json').write_bytes(canonical_bytes(comparison))
    if candidate=='C1_T' and comparison['r2']['status']!='PASS':raise ValueError('ORDERED_HOST_MODEL_CALIBRATION_FAILED_REVIEW_REFERENCE_BEFORE_CANDIDATES')
    reports[candidate]=dict(logits_sha256=r['logits_sha256'],stages=len(r['stages']),gn_layers=len(r['gn_stats']),numeric=comparison,scope=r['scope'],gpu_cycles=None)
assert reports['C1_T']['stages']==reports['G1']['stages']==100
result=dict(scope='HOST_NUMERIC_MODEL_NOT_TCU_NOT_GPU_NOT_A53',historical=review,candidates=reports,board='NOT_RUN')
(out/'summary.json').write_bytes(canonical_bytes(result));print(json.dumps(dict(out=str(out),result=result)))
