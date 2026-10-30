"""Numerical policy adapter; original thresholds/NRMSE implementation are unchanged."""
import hashlib,importlib.util,json,math,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
SOURCE_SHA='cd71119814ea63854f3eba459596c517261155906e52b80a2ac5c8e53eb93067'
R2=dict(atol=1e-3,rtol=1e-3,nrmse_limit=5e-4)
ORT=dict(atol=1e-4,rtol=1e-4,nrmse_limit=1e-4)
def frozen_compare():
    p=ROOT/'scripts/hunet/audit_run.py'
    if hashlib.sha256(p.read_bytes()).hexdigest()!=SOURCE_SHA:raise ValueError('FROZEN_NUMERIC_SOURCE_CHANGED')
    spec=importlib.util.spec_from_file_location('opt_frozen_numeric',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    return m.compare
def values(raw):
    if not isinstance(raw,bytes) or len(raw)!=32420:raise ValueError('FULL_8105_FP32_OUTPUT_REQUIRED')
    return list(struct.unpack('<8105f',raw))
def compare_outputs(actual,refs,history):
    a=values(actual);r2=values(refs.get('r2'));ort=values(refs.get('ort'));old=values(history)
    if any(not math.isfinite(x) for x in r2+ort+old):raise ValueError('REFERENCE_NONFINITE')
    if refs.get('r2_contract',R2)!=R2 or refs.get('ort_contract',ORT)!=ORT:raise ValueError('FROZEN_THRESHOLDS_CHANGED')
    if any(not math.isfinite(x) for x in a):
        return dict(classification='RUNTIME_FAILURE',runtime_finite=False,eligible_for_combine=False,formal_numeric_pass=False,r2={'status':'NOT_EVALUATED'},ort={'status':'NOT_EVALUATED'},history={'byte_identical':False},prediction={'recognition_accuracy':'UNAVAILABLE'})
    compare=frozen_compare();r=compare(a,r2,R2);o=compare(a,ort,ORT);same=actual==history
    classification='NUMERIC_PASS' if r['status']==o['status']=='PASS' else 'KNOWN_PRECISION_DEVIATION' if same and r['status']=='PASS' else 'REVIEW_REQUIRED'
    rank=lambda x:sorted(range(len(x)),key=lambda i:(-x[i],i))[:5]
    top=rank(a);rt=rank(r2);ot=rank(ort);ht=rank(old)
    prediction=dict(top1_index=top[0],top5_indices=top,r2_top1_match=top[0]==rt[0],ort_top1_match=top[0]==ot[0],history_top1_match=top[0]==ht[0],history_top5_indices=ht,r2_top5_set_match=set(top)==set(rt),ort_top5_set_match=set(top)==set(ot),history_top5_set_match=set(top)==set(ht),labels='UNAVAILABLE',recognition_accuracy='UNAVAILABLE')
    if refs.get('labels') is not None:
        raw=refs['labels']
        if hashlib.sha256(raw).hexdigest()!='ce1a0e8c2abcd4c58438885d6697102a7b0379614c90ed1691e3fc7af86c807f':raise ValueError('FROZEN_LABEL_IDENTITY')
        labels=json.loads(raw.decode('utf-8'))
        if set(labels)!=set(str(i) for i in range(8105)):raise ValueError('LABEL_INDEX_COVERAGE')
        prediction['labels']=[labels[str(i)] for i in top]
    delta=[float(x)-float(y) for x,y in zip(a,old)];rmse=math.sqrt(sum(x*x for x in delta)/len(delta));denom=math.sqrt(sum(float(x)*float(x) for x in old)/len(old))
    return dict(classification=classification,runtime_finite=True,r2=r,ort=o,
        history=dict(byte_identical=same,actual_sha256=hashlib.sha256(actual).hexdigest(),historical_sha256=hashlib.sha256(history).hexdigest(),fp32_word_difference_count=sum(actual[i:i+4]!=history[i:i+4] for i in range(0,len(actual),4)),numeric_difference_count=sum(x!=y for x,y in zip(a,old)),max_abs=max(abs(x) for x in delta),rmse=rmse,nrmse=rmse/denom if denom else (0.0 if rmse==0 else None),nrmse_zero_reference='ZERO_IF_EQUAL_ELSE_UNDEFINED',count_semantics='FP32_WORD_BYTES_AND_SEPARATE_NUMERIC_VALUES'),
        prediction=prediction,
        eligible_for_combine=r['status']=='PASS',formal_numeric_pass=r['status']==o['status']=='PASS',numeric_source_sha256=SOURCE_SHA)
