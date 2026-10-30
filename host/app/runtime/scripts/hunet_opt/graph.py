"""Graph admission from immutable logical data, before optional transformations."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import copy
ROOT=Path(__file__).resolve().parents[2]
BASE_CANDIDATES=('C1_T','C1_H','G1','G2_LOCATE','G2_PACK','G2_WMMA2','G2_WMMA4')
CANDIDATES=BASE_CANDIDATES+('G3_NORM','G3_VIEWS','G4','G4_C1','G4_C16','G2_TIMING','COMBINED')
def frozen():
    p=ROOT/'scripts/hunet_fused/catalog.py'
    spec=importlib.util.spec_from_file_location('opt_frozen_fused_compiler',p)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    historical=json.loads((ROOT/'contracts/hunet_fused_v1.json').read_text())
    archived=json.loads((ROOT/'contracts/baselines/hunet-v5-before-w4.json').read_text())
    module.hybrid_freezer(ROOT).verify_baseline(historical['baseline'],archived)
    contract=module.freeze_catalog(ROOT)
    if any(contract[k]!=historical[k] for k in ('jobs','matrices','schema','policy','program_id','source_base')):raise ValueError('WORKLOAD_CHANGED_DURING_HARDWARE_REBIND')
    schedule=module.compile_schedule(contract)
    manifest=json.loads((ROOT/'generated/hunet_fused/manifest.json').read_text())
    for name,sha in manifest['files'].items():
        raw=(ROOT/'generated/hunet_fused'/name).read_bytes()
        if hashlib.sha256(raw).hexdigest()!=sha:
            # Historical 4fd50b8 added only a compiler unused annotation without
            # refreshing this old manifest. Require exact pre-annotation bytes.
            if name!='layout.h' or hashlib.sha256(raw.replace(b' __attribute__((unused))',b'')).hexdigest()!=sha:raise ValueError('FROZEN_FUSION_DRIFT:'+name)
    layout=json.loads((ROOT/'generated/hunet_fused/layout.json').read_text())
    return contract,schedule,layout
def build_blocks(logical:list,cpu_stages:list,candidate:str)->list:
    if candidate not in CANDIDATES:raise ValueError('CANDIDATE_NOT_IMPLEMENTED:'+candidate)
    contract,schedule,layout=frozen()
    if logical!=contract['jobs'] or cpu_stages!=schedule['cpu']:raise ValueError('LOGICAL_OR_CPU_IDENTITY')
    words=list(struct.iter_unpack('<32I',(ROOT/'generated/hunet_fused/jobs.bin').read_bytes()))
    blocks=[]
    for item,job in zip(schedule['gpu'],words):
        job=list(job)
        if job[3]==15:job[15]=0x84040040
        blocks.append(dict(kind='GPU',first=item['first'],last=item['last'],first_stage=item['stage'],last_stage=item['stage'],op=item['op'],words=job,block_id=0,owner_tiles=job[17] if job[3]==15 else 0,materialized_stages=[item['stage']],virtual_stages=[],nodes=[]))
    for cpu in schedule['cpu']:
        q=cpu['sequence'];blocks.append(dict(kind='CPU',first=q,last=q,first_stage=cpu['stage'],last_stage=cpu['stage'],op=6,words=list(logical[q-1]),block_id=0,owner_tiles=0,materialized_stages=[cpu['stage']],virtual_stages=[],nodes=[]))
    blocks=sorted(blocks,key=lambda e:e['first'])
    return fold_norm(blocks) if candidate in ('G3_NORM','G3_VIEWS','COMBINED') else blocks
def entry_reads(entry):
    if entry['op'] in (16,17):return {n[i] for n in entry['nodes'] for i in (4,5,7,8,9,10,11) if n[i]}
    w=entry['words']
    if entry['op']==15:return {x for x in w[8:11] if x}
    return {w[i] for i in (8,9,10,11,12,14,15) if w[i]}
def entry_output(entry):return entry['words'][11] if entry['op']==15 else entry['words'][13]
def gn_pair_can_fuse(entries,index):
    if index+1>=len(entries):return False
    a,b=entries[index:index+2]
    if (a['op'],b['op'])!=(4,5) or a['kind']!='GPU' or b['kind']!='GPU' or a['last']+1!=b['first']:return False
    logical=frozen()[0]['jobs'];aw,bw=a['words'],b['words']
    if aw!=logical[a['first']-1] or bw!=logical[b['first']-1]:return False
    if aw[8]!=bw[8] or aw[13]!=bw[14] or aw[16:22]!=bw[16:22] or aw[28]!=0x3727c5ac:return False
    n=aw[16]*aw[18]*aw[19];x,y=aw[8],bw[13]
    if not n or x<y+n*4 and y<x+n*4:return False
    consumers=[]
    for i in range(index+1,len(entries)):
        if aw[13] in entry_reads(entries[i]):consumers.append(i)
        if entry_output(entries[i])==aw[13]:break
    return consumers==[index+1]
def node_words(entry):
    w=entry['words'];op=entry['op']
    if op==6 or not 1<=op<=15:raise ValueError('GPU_NODE_TYPE')
    if op==15:
        return [15,entry['first_stage'],entry['first'],entry['last'],w[8],0,w[11],w[9],w[10],0,0,w[14],w[18],w[19],w[20],w[21],w[22],w[23],w[24],w[25],w[26],w[7],w[17],w[27]]
    return [op,entry['first_stage'],entry['first'],entry['last'],w[8],w[9],w[13],0,w[10],w[11],w[12],w[14] if op==5 else w[15]]+w[16:22]+[w[22],w[24],w[25],w[26],w[7],w[5]]
def fold_norm(entries):
    result=[];index=0
    while index<len(entries):
        if gn_pair_can_fuse(entries,index):
            a,b=entries[index:index+2];result.append(dict(kind='GPU',first=a['first'],last=b['last'],first_stage=a['first_stage'],last_stage=b['last_stage'],op=16,words=[],block_id=0,owner_tiles=0,materialized_stages=sorted(set(a['materialized_stages']+b['materialized_stages'])),virtual_stages=[],nodes=[node_words(a),node_words(b)]));index+=2
        else:result.append(copy.deepcopy(entries[index]));index+=1
    return result
def assemble_graph(entries):
    slots=[]
    for entry in entries:
        if entry['op'] not in (16,17):continue
        nodes=entry['nodes'];block=len(slots)+1
        if block>64 or not 1<=len(nodes)<=32 or entry['owner_tiles']>262144:raise ValueError('GRAPH_CAPACITY')
        size=(128+96*len(nodes)+63)&~63
        header=[0x32444748,2,size,block,entry['first'],entry['last'],entry['first_stage'],entry['last_stage'],len(nodes),2,0x86c4df40,4096]+[0]*20
        raw=struct.pack('<32I',*header)+b''.join(struct.pack('<24I',*n) for n in nodes);raw+=bytes(size-len(raw))
        entry['block_id']=block
        entry['words']=[0x32474c48,2,128,entry['op'],entry['first'],entry['last'],entry['first_stage'],entry['last_stage'],block,0x84000000+(block-1)*4096,size,0x86c4df40,4096,0x84040040 if entry['owner_tiles'] else 0,entry['owner_tiles'],2]+[0]*16
        slots.append(dict(id=block,bytes=size,hex=raw.hex(),sha256=hashlib.sha256(raw).hexdigest()))
    return slots
def view_window_can_fold(entries,index):
    if index<0 or index+5>len(entries):return False
    window=entries[index:index+5]
    if [e['op'] for e in window]!=[12,12,15,13,15] or any(e['kind']!='GPU' for e in window):return False
    if any(a['last']+1!=b['first'] for a,b in zip(window,window[1:])):return False
    a,b,m,c,z=[e['words'] for e in window]
    logical=frozen()[0]['jobs']
    if any(e['words']!=logical[e['first']-1] for e in (window[0],window[1],window[3])):return False
    if a[8]!=b[8] or a[26]!=0 or b[26]!=a[17] or a[17]+b[17]!=a[16]:return False
    if m[8]!=a[13] or c[8]!=m[11] or c[9]!=b[13] or z[8]!=c[13]:return False
    if a[18:22]!=b[18:22] or m[20:24]!=a[18:22] or z[20:22]!=c[18:20]:return False
    source_begin=a[8];source_end=source_begin+a[16]*a[18]*a[19]*4
    for matrix in (m,z):
        begin=matrix[11];end=begin+matrix[19]*matrix[22]*matrix[23]*4
        if begin<source_end and source_begin<end:return False
    if m[17]+z[17]>262144:return False
    virtual_outputs={a[13],b[13],c[13]}
    for i,e in enumerate(entries):
        if index<=i<index+5:continue
        if virtual_outputs&entry_reads(e):return False
    return True
def fold_view_entries(entries):
    out=[];index=0;avoided=0;retained=[]
    for e in entries:
        if e['op']==7:retained.append(dict(stage=e['first_stage'],reason='current GN consumer requires contiguous materialization; no unsafe bypass'))
    while index<len(entries):
        if not view_window_can_fold(entries,index):out.append(copy.deepcopy(entries[index]));index+=1;continue
        window=entries[index:index+5];a,b,m,c,z=[e['words'] for e in window]
        first_segment=dict(dst_first=0,count=a[17],source_gpu=a[8],source_first=a[26],source_ci=a[16],source_h=a[18],source_w=a[19],dy=0,dx=0)
        merged_segments=[dict(dst_first=0,count=m[19],source_gpu=m[11],source_first=0,source_ci=m[19],source_h=m[22],source_w=m[23],dy=0,dx=0),dict(dst_first=m[19],count=b[17],source_gpu=b[8],source_first=b[26],source_ci=b[16],source_h=b[18],source_w=b[19],dy=0,dx=0)]
        views=[dict(node_index=2,channels=m[18],height=m[20],width=m[21],segments=[first_segment]),dict(node_index=4,channels=z[18],height=z[20],width=z[21],segments=merged_segments)]
        if any(len(v['segments'])>32 for v in views):out.extend(copy.deepcopy(window));index+=5;continue
        virtual=[window[i]['first_stage'] for i in (0,1,3)]
        saved=sum(w[17]*w[20]*w[21]*4 for w in (a,b,c));avoided+=saved
        out.append(dict(kind='GPU',first=window[0]['first'],last=window[-1]['last'],first_stage=window[0]['first_stage'],last_stage=window[-1]['last_stage'],op=17,words=[],block_id=0,owner_tiles=m[17]+z[17],materialized_stages=[window[2]['first_stage'],window[4]['first_stage']],virtual_stages=virtual,nodes=[node_words(e) for e in window],view_sources=views,virtual_nodes=[0,1,3],avoided_materialized_write_bytes=saved,avoided_materialized_read_bytes=saved))
        index+=5
    report=dict(saved_materialization_bytes=2*avoided,avoided_write_bytes=avoided,avoided_read_bytes=avoided,scope='static eliminated intermediate accesses; not net DRAM traffic or measured speed',retained_shift_stages=retained)
    return out,report
def fold_views(catalog:dict)->dict:
    from scripts.hunet_opt.catalog import validate_catalog,compile_catalog
    validate_catalog(catalog)
    if catalog['candidate']!='G3_NORM':raise ValueError('VIEW_TRANSFORM_BASE')
    return compile_catalog('G3_VIEWS')
