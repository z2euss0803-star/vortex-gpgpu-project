"""Independently parse VCD observations of bus completions and reset repeat."""
from pathlib import Path
import hashlib
def audit(path):
    scope=[];ids={};values={};phases=set();reset=[];ready=[];header=True;digest=hashlib.sha256()
    names=['phase','resetn_observed','ready_observed','ddr_resetn_observed','dma_jobs','gpu_rounds',
           'hpm0_reads','hpm0_writes','hpm1_reads','hpm1_writes','hp_reads','hp_writes',
           'ddr_reads','ddr_writes','gpu_reads','gpu_writes','host_reads','host_writes',
           'host_flushes','host_invalidates','host_barriers','gpu_flush_issued','ps_flush_accepted']
    with Path(path).open('rb') as stream:
        for raw in stream:
            digest.update(raw);line=raw.decode('ascii',errors='replace').strip();parts=line.split()
            if header:
                if line.startswith('$scope'):scope.append(parts[2])
                elif line.startswith('$upscope'):scope.pop()
                elif line.startswith('$var') and scope==['tb_ps_host_c'] and parts[4] in names:ids[parts[3]]=parts[4]
                elif line.startswith('$enddefinitions'):header=False
                continue
            if not line or line[0] in '#$':continue
            if line[0] in 'bB' and len(parts)==2:binary,code=parts[0][1:],parts[1]
            elif line[0] in '01xXzZ':binary,code=line[0],line[1:]
            else:continue
            if code not in ids or any(x not in '01' for x in binary):continue
            key=ids[code];value=int(binary,2);values[key]=value
            if key=='phase':phases.add(value)
            if key=='resetn_observed' and (not reset or reset[-1]!=value):reset.append(value)
            if key=='ready_observed' and (not ready or ready[-1]!=value):ready.append(value)
    assert set(names)<=values.keys(),set(names)-values.keys()
    assert {1,3,4,5,6,7}<=phases,phases
    assert reset[-4:]==[0,1,0,1] and ready[-4:]==[0,1,0,1],(reset,ready)
    assert values['dma_jobs']==66 and values['gpu_rounds']==6,values
    assert values['gpu_flush_issued']==values['ps_flush_accepted']==96,values
    for name in names[6:]:assert values[name]>0,(name,values[name])
    return {'pass':True,'sha256':digest.hexdigest(),'phases':sorted(phases),'reset':reset,'ready':ready,'final_counters':values}
