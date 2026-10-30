"""Evidence-layer matrix. No global PASS from partial or synthetic work."""
import re
LAYERS=('SW','CROSS_DEVICE','CROSS_CPU','CROSS_KERNEL','CROSS_BINDING','IMAGE','BOARD_PLATFORM','BOARD_S8','COMPAT','PERF')
SCOPES=dict(SW='UBUNTU_SOFTWARE_ONLY',CROSS_DEVICE='RV32_STATIC_BUILD',CROSS_CPU='AARCH64_BUILD',CROSS_KERNEL='KERNEL_PRE_BINDING',CROSS_BINDING='KERNEL_BOUND_BUILD',IMAGE='IMAGE',BOARD_PLATFORM='REAL_BOARD',BOARD_S8='REAL_BOARD',COMPAT='REAL_BOARD',PERF='REAL_BOARD')
def summarize(receipts):
    states={k:'NOT_RUN' for k in LAYERS};source=None;cycles=None;errors=[]
    for r in receipts:
        layer=r['layer'];state=r['state'];digest=r['source_sha256']
        if layer not in states or not re.fullmatch('[0-9a-f]{64}',digest) or r['scope']!=SCOPES[layer]:raise ValueError('invalid scope/source/layer')
        if source is not None and source!=digest:raise ValueError('mixed source receipts')
        source=digest
        if states[layer]!='NOT_RUN':raise ValueError('duplicate layer receipt')
        if type(r['exit']) is not int or state=='PASS' and r['exit']!=0:raise ValueError('missing/nonzero natural exit cannot PASS')
        if state=='PASS' and r.get('retest_required') and r.get('retest_done') is not True:raise ValueError('required affected-source retest missing')
        if state not in ('PASS','FAIL','PENDING','WAIT_HW_EXPORT','RESOURCE_REFUSED','NOT_RUN','PRE_BINDING'):raise ValueError('unknown state')
        if layer=='CROSS_KERNEL' and state=='PASS':state='PRE_BINDING'
        if layer in ('BOARD_PLATFORM','BOARD_S8','COMPAT','PERF') and state=='PASS':
            if not r.get('independent_audit') or r.get('origin')!='TRUSTED_CONTROLLER_REAL_BOARD':raise ValueError('missing real field evidence')
        if layer=='PERF' and state=='PASS':
            cycles=r['gpu_raw_cycles']
            if type(cycles) is not list or not cycles or any(type(v) is not int or not 0<v<4000000000 for v in cycles):raise ValueError('actual raw cycles required')
        states[layer]=state;errors.extend(r.get('errors',[]))
    return dict(version=1,source_sha256=source,states=states,all_pass=all(v=='PASS' for v in states.values()),gpu_raw_cycles=cycles,gpu_seconds=None,errors=errors,physical_status='OPEN_NOT_PASS')
