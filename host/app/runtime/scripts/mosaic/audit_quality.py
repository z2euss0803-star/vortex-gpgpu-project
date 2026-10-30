"""Independent frozen W16A16 compatibility receipts; no board admission."""
import math

def digest(value):
    if not isinstance(value,str) or len(value)!=64 or any(c not in '0123456789abcdef' for c in value):
        raise ValueError('invalid digest')
    return value

def audit_cases(binding,cases,allow_partial=False):
    frozen=binding.get('rows')
    if not isinstance(frozen,list) or len(frozen)!=21 or [r.get('index') for r in frozen]!=list(range(21)):
        raise ValueError('frozen 21-case identity')
    seen=set();records=[]
    for case in cases:
        idx=case.get('row')
        if type(idx) is not int or idx not in range(21) or idx in seen:raise ValueError('duplicate/unknown case')
        seen.add(idx);result=case['result']
        if type(result.get('compile_exit')) is not int or result['compile_exit']!=0 or type(result.get('natural_exit')) is not int or result['natural_exit']!=0:
            raise ValueError('failed build/run')
        if result.get('thresholds_changed') is not False or result.get('default_changed') is not False:
            raise ValueError('changed contract/default')
        if 'gpu_raw_cycles' not in result or result['gpu_raw_cycles'] is not None:raise ValueError('host is not measured GPU')
        if digest(result['input_archive_sha256'])!=digest(case['expected_input_archive_sha256']):raise ValueError('input identity')
        layers=result.get('layers')
        if not isinstance(layers,list) or len(layers)!=16 or [x.get('stage') for x in layers]!=list(range(16)):raise ValueError('missing/reordered layers')
        refs={x['index']:x for x in frozen[idx]['raw'] if x['branch']=='W16A16'}
        if set(refs)!=set(range(16)):raise ValueError('missing frozen INT16 outputs')
        outputs=result.get('outputs',{})
        if set(outputs)!=set('candidate-stage%02d.f32'%i for i in range(16)):raise ValueError('missing/extra candidate outputs')
        for i,layer in enumerate(layers):
            if layer.get('pass') is not True or layer.get('gpu_raw_cycles','MISSING') is not None:raise ValueError('layer scope/status')
            for key in ('max_abs','rmse'):
                value=layer.get(key)
                if type(value) not in (int,float) or not math.isfinite(value) or value!=0:raise ValueError('not bitexact/finite')
            for key in ('violations','different_bits'):
                if type(layer.get(key)) is not int or layer[key]!=0:raise ValueError('numeric differences')
            out=outputs['candidate-stage%02d.f32'%i];ref=refs[i]
            if type(out.get('bytes')) is not int or out['bytes']<=0 or out['bytes']!=ref['bytes'] or digest(out['sha256'])!=digest(ref['sha256']):raise ValueError('output bytes/digest mismatch')
        records.append(dict(row=idx,shape=frozen[idx]['shape'],verified_layer_outputs=16,final_sha256=outputs['candidate-stage15.f32']['sha256']))
    complete=seen==set(range(21))
    if not complete and not allow_partial:raise ValueError('incomplete formal set')
    return dict(scope='HOST_FROZEN_W16A16_COMPAT_NOT_GPU_NOT_BOARD',complete=complete,verified_cases=len(records),verified_layer_outputs=16*len(records),missing_cases=sorted(set(range(21))-seen),records=sorted(records,key=lambda x:x['row']),gpu_raw_cycles=None)
