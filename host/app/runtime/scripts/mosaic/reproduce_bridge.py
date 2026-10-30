"""Read-only old last-layer reproduction and first-cause separation, never hardware.

This diagnostic does NOT implement or accept the new full-network candidate.
"""
import argparse,ctypes,hashlib,json,os,subprocess,sys,time
from pathlib import Path
from scripts.demo.identity import sha256_file,verify_sources


def numpy_scalar(value):
    import numpy as np
    if isinstance(value,np.generic):return value.item()
    raise TypeError('unsupported JSON value '+str(type(value)))


def resource_snapshot():
    if os.name=='nt':
        u64=ctypes.c_ulonglong;u32=ctypes.c_ulong
        class Memory(ctypes.Structure):
            _fields_=[('length',u32),('load',u32)]+[(n,u64) for n in ('total_phys','avail_phys','total_commit','avail_commit','total_va','avail_va','extended')]
        m=Memory();m.length=ctypes.sizeof(m)
        if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m)):raise RuntimeError('memory unknown')
        query="@(Get-Process -Name vivado,xsim,xsimk,vsim,yosys -ErrorAction SilentlyContinue | Select-Object ProcessName,Id) | ConvertTo-Json -Compress"
        proc=subprocess.run(['powershell','-NoProfile','-Command',query],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        raw=proc.stdout.decode('utf-8','replace').strip()
        heavy=json.loads(raw) if raw else []
        return dict(scope='A_WINDOWS_FRESH_RESOURCE',available_ram=m.avail_phys,available_commit=m.avail_commit,other_heavy=heavy)
    text=Path('/proc/meminfo').read_text()
    mem=int(next(x.split()[1] for x in text.splitlines() if x.startswith('MemAvailable:')))*1024
    raise RuntimeError('Ubuntu numeric admission also requires C lock/coordination; use admitted wrapper, RAM='+str(mem))


def reproduce_bridge(manifest,out):
    out=Path(out);out.mkdir(parents=True,exist_ok=False)
    resource=resource_snapshot();(out/'resources.json').write_text(json.dumps(resource,indent=2)+'\n',encoding='utf-8')
    if resource['available_ram']<4*1024**3 or resource.get('available_commit',0)<8*1024**3 or resource['other_heavy']:
        raise RuntimeError('fresh numerical resource gate refused; no numerical worker started')
    admission=verify_sources(manifest)
    sources=json.loads(Path(manifest).read_text(encoding='utf-8'));native=Path(sources['roots']['native'])
    old=native/'runs/20261008T151822687476Z_numeric_bridge_cd8d3425'
    result=json.loads((old/'image_01/result.json').read_text(encoding='utf-8'))
    binding=json.loads((native/'config/bridge_source.json').read_text(encoding='utf-8'))
    model=json.loads((native/'inputs/model.json').read_text(encoding='utf-8'))
    records=[]
    def check(path,record):
        path=Path(path)
        if path.stat().st_size!=record['bytes'] or sha256_file(path)!=record['sha256']:raise ValueError('old diagnostic source identity drift '+str(path))
        records.append(dict(path=str(path),**{key:record[key] for key in ('bytes','sha256')}));return path
    previous=result['layers'][14];last=result['layers'][15]
    native_input=check(old/'image_01/14_deconv2.bin',previous['output'])
    native_output=check(old/'image_01/15_deconv3.bin',last['output'])
    frozen={r['index']:r for r in binding['rows'][0]['raw'] if r['branch']=='W16A16' and r['index'] in (14,15)}
    frozen_input=check(frozen[14]['path'],frozen[14]);frozen_output=check(frozen[15]['path'],frozen[15])
    package=check(binding['native_package']['path'],binding['native_package'])
    for path in list((native/'reference').glob('*.py'))+[native/'config/bridge_source.json',native/'inputs/model.json',old/'image_01/result.json']:
        records.append(dict(path=str(path),bytes=path.stat().st_size,sha256=sha256_file(path)))
    (out/'source-before.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
    os.environ['OMP_NUM_THREADS']='1';os.environ['OPENBLAS_NUM_THREADS']='1';os.environ['MKL_NUM_THREADS']='1'
    import numpy as np
    sys.path.insert(0,str(native))
    from reference.network import NativeNetwork
    from reference.quality import error
    from reference.conv_gather import gather_positions
    from scripts.mosaic.reference import quantize_rne,dot_i64,restore_f32
    network=NativeNetwork(package.read_bytes(),model)
    x=np.fromfile(native_input,dtype='<f4').reshape(previous['output_shape'])
    x_frozen=np.fromfile(frozen_input,dtype='<f4').reshape(previous['output_shape'])
    saved=np.fromfile(native_output,dtype='<f4').reshape(last['output_shape'])
    expected=np.fromfile(frozen_output,dtype='<f4').reshape(last['output_shape'])
    begin=time.monotonic();y=network.call(x,15);seconds=time.monotonic()-begin
    y.tofile(str(out/'native-last-layer.bin'))
    (out/'native-time.json').write_text(json.dumps(dict(clock_domain='A_HOST_MONOTONIC',seconds=seconds))+'\n')
    if not np.array_equal(y.view(np.uint32),saved.view(np.uint32)):
        raise ValueError('old native last-layer not reproduced exactly; keep result and investigate environment')
    begin=time.monotonic();counter=network.call(x_frozen,15);counter_seconds=time.monotonic()-begin
    counter.tofile(str(out/'frozen-input-last-layer.bin'))
    (out/'counterfactual-time.json').write_text(json.dumps(dict(clock_domain='A_HOST_MONOTONIC',seconds=counter_seconds))+'\n')
    delta=np.abs(y.astype(np.float64)-expected.astype(np.float64));coord=tuple(int(i) for i in np.unravel_index(int(np.argmax(delta)),delta.shape))
    _,channel,row,col=coord;params=network.package[15]['arrays'];sa=params['sA'][0]
    inverse=np.divide(np.float32(1),sa,dtype=np.float32)
    q=quantize_rne(x,inverse);q_frozen=quantize_rne(x_frozen,inverse)
    pos=np.array([row*y.shape[3]+col],dtype=np.int64)
    patch=gather_positions(q[0],pos,9,1,1);other=gather_positions(q_frozen[0],pos,9,1,1)
    w=params['q16'][:,:network.package[15]['K']]
    accumulator=dot_i64(w,patch);other_accumulator=dot_i64(w,other)
    restored=restore_f32(accumulator,params['coeff'][:,0],sa,params['coeff'][:,1])
    if not np.array_equal(restored[:,0].view(np.uint32),y[0,:,row,col].view(np.uint32)):raise ValueError('independent exact-dot/restore mismatch')
    np.savez(str(out/'worst-coordinate.npz'),weight=w,input_patch=patch,frozen_input_patch=other,accumulator=accumulator,other_accumulator=other_accumulator,restored=restored)
    summary=dict(scope='HOST_OLD_LAST_LAYER_REPRODUCTION_NOT_FULL_CANDIDATE_PASS',source_admission=admission,
                 old_native_last_layer_exact=True,old_failure=error(y,expected),old_layer_contract=error(y,expected,layer=True),
                 frozen_input_same_last_layer=error(counter,expected),
                 upstream_difference=error(x,x_frozen,layer=True),worst_coordinate=coord,
                 changed_q_in_worst_patch=int(np.count_nonzero(patch!=other)),
                 native_accumulator=[int(v) for v in accumulator[:,0]],frozen_input_accumulator=[int(v) for v in other_accumulator[:,0]],
                 independent_integer_restore_exact_at_coordinate=True,native_seconds=seconds,counterfactual_seconds=counter_seconds,
                 candidate_repaired=False,gpu_raw_cycles=None,board='NOT_RUN',numpy=np.__version__,python=sys.version)
    (out/'result.json').write_text(json.dumps(summary,indent=2,default=numpy_scalar)+'\n',encoding='utf-8')
    after=[]
    for record in records:
        path=Path(record['path'])
        actual=dict(path=str(path),bytes=path.stat().st_size,sha256=sha256_file(path))
        if actual!=record:raise ValueError('diagnostic source changed during run')
        after.append(actual)
    (out/'source-after.json').write_text(json.dumps(after,indent=2)+'\n',encoding='utf-8')
    # Cause separation is evidence only; no automatic new candidate or full acceptance.
    print(json.dumps(summary,indent=2,default=numpy_scalar));return summary


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--manifest',required=True);parser.add_argument('--out',required=True);args=parser.parse_args()
    reproduce_bridge(args.manifest,args.out)
