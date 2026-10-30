"""Reuse only the old scratch-C arena; preserve every network tensor slot."""
import json
import pathlib
from catalog import compile_schedule

ROOT=pathlib.Path(__file__).resolve().parents[2]


def align64(value):
    return (value+63)&~63


def original_regions():
    return json.loads((ROOT/'contracts/hunet_v5.json').read_text(encoding='utf-8'))['graph']['layout']


def compile_layout(contract,schedule):
    if schedule!=compile_schedule(contract):
        raise ValueError('SCHEDULE_NOT_BOUND_TO_LOGICAL_CATALOG')
    regions=original_regions()
    arena=next(x for x in regions if x['name']=='scratch_C_fp32')
    stages=[]
    descriptor_index=0
    for item in schedule['gpu']:
        if item['op']!=15:continue
        rows=[]
        b_offset=pack_base=gemm_base=0
        for entry in item['descriptors']:
            matrix=entry['matrix']
            rows.append(matrix+[b_offset,pack_base,gemm_base,entry['first'],entry['last'],0,0,0])
            b_offset=align64(b_offset+matrix[4]*matrix[5]*2)
            pack_base+=matrix[5]
            gemm_base+=(matrix[3]//8)*(matrix[4]//4)
        first=item['descriptors'][0]
        pack,gemm,unpack=first['pack'],first['gemm'],first['unpack']
        stages.append(dict(first=item['first'],last=item['last'],stage_id=item['stage'],
            descriptors=rows,descriptor_index=descriptor_index,packed_b_bytes=b_offset,
            pack_grid_rows=pack_base,gemm_grid_tiles=gemm_base,
            input_gpu=pack[8],weights_gpu=gemm[9],bias_gpu=unpack[10],output_gpu=unpack[13],
            shape=pack[16:23]+[pack[24],pack[25]],flags=pack[5]))
        descriptor_index+=len(rows)
    b_bytes=max(x['packed_b_bytes'] for x in stages)
    descriptor_bytes=descriptor_index*64
    trace_bytes=max(x['gemm_grid_tiles'] for x in stages)*4
    descriptor_start=align64(arena['start']+b_bytes+64)
    trace_start=align64(descriptor_start+descriptor_bytes+64)
    outputs=[]
    for stage in range(100):
        last=[j for j in contract['jobs'] if j[30]==stage][-1]
        slot=next(r for r in regions if r['start']==last[13])
        outputs.append(dict(stage_id=stage,last_sequence=last[4],gpu=last[13],
                            bytes=32420 if stage==99 else slot['bytes']))
    layout=dict(schema=1,scope='STATIC_FUSED_LAYOUT_NOT_GPU_PASS',workspace_base=0x85000000,
        workspace_bytes=33976320,arena=arena,original_regions=regions,stage_outputs=outputs,
        packed_b=dict(start=arena['start'],bytes=b_bytes),
        descriptors=dict(start=descriptor_start,bytes=descriptor_bytes),
        owner_trace=dict(start=trace_start,bytes=trace_bytes),
        guards=[arena['start']+b_bytes,descriptor_start+descriptor_bytes,trace_start+trace_bytes],
        matrix_stages=stages)
    validate_layout(layout)
    return layout


def validate_layout(layout):
    if layout['schema']!=1 or (layout['workspace_base'],layout['workspace_bytes'])!=(0x85000000,33976320):
        raise ValueError('WORKSPACE_IDENTITY')
    regions=original_regions()
    arena=next(r for r in regions if r['name']=='scratch_C_fp32')
    if layout['original_regions']!=regions or layout['arena']!=arena:
        raise ValueError('ORIGINAL_LAYOUT_CHANGED')
    if [layout[k]['bytes'] for k in ('packed_b','descriptors','owner_trace')]!=[786432,20096,49152]:
        raise ValueError('FUSED_REGION_CAPACITY')
    end=arena['start']
    for name,guard in zip(('packed_b','descriptors','owner_trace'),layout['guards']):
        span=layout[name];start,size=span['start'],span['bytes']
        if start%64 or start<end or not size or start+size+64>min(0x100000000,arena['start']+arena['bytes']):
            raise ValueError('FUSED_REGION_ALIAS_OR_RANGE')
        if guard!=start+size or guard%64:
            raise ValueError('FUSED_GUARD_LOCATION')
        # Every old guard remains outside the repurposed arena's interior.
        for old in regions:
            for address in (old['start']-64,old['start']+old['bytes']):
                if start<address+64 and address<start+size:
                    raise ValueError('OLD_GUARD_OVERWRITES_METADATA')
        end=guard+64
    if len(layout['guards'])!=3 or len(layout['matrix_stages'])!=35:
        raise ValueError('STAGE_OR_GUARD_COUNT')
    index=0
    for stage in layout['matrix_stages']:
        rows=stage['descriptors']
        if not 1<=len(rows)<=32 or stage['descriptor_index']!=index:
            raise ValueError('STAGE_DESCRIPTOR_TABLE')
        b_offset=pack_base=gemm_base=0
        expected_sequence=stage['first']
        output_offset=0
        for d in rows:
            if len(d)!=16 or any(d[13:]):raise ValueError('DESCRIPTOR_WIRE')
            m,n,k,mp,np,kp,offset,count=d[:8]
            if not (m>0 and n>0 and k>0 and mp>=m and mp%8==0 and
                    np>=count>0 and np%4==0 and kp>=k and kp%8==0 and
                    offset==output_offset and offset+count<=n):
                raise ValueError('DESCRIPTOR_SHAPE_OR_OUTPUT_COVERAGE')
            if d[8:13]!=[b_offset,pack_base,gemm_base,expected_sequence,expected_sequence+2]:
                raise ValueError('DESCRIPTOR_PARTITION_OR_PREFIX')
            b_offset=align64(b_offset+kp*np*2)
            pack_base+=kp;gemm_base+=(mp//8)*(np//4)
            expected_sequence+=3;output_offset+=count
        if output_offset!=rows[0][1] or expected_sequence!=stage['last']+1:
            raise ValueError('STAGE_LOGICAL_OR_OUTPUT_COVERAGE')
        if [b_offset,pack_base,gemm_base]!=[stage['packed_b_bytes'],stage['pack_grid_rows'],stage['gemm_grid_tiles']]:
            raise ValueError('STAGE_PREFIX_TOTAL')
        if b_offset>786432 or gemm_base>12288 or stage['flags'] not in (0,2):
            raise ValueError('STAGE_BOUND')
        if (stage['input_gpu']%4 or stage['weights_gpu']%2 or stage['output_gpu']%4 or
                any(not 0<=value<0x100000000 for value in
                    [stage['input_gpu'],stage['weights_gpu'],stage['bias_gpu'],stage['output_gpu']])):
            raise ValueError('GPU32_ADDRESS')
        index+=len(rows)
    if index!=314:raise ValueError('DESCRIPTOR_COUNT')
