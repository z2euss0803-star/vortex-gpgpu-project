"""Deterministic program10 catalog; data never comes from a runtime request."""
import copy
import hashlib
import struct
import json
import re
from pathlib import Path
from scripts.hunet_opt.graph import BASE_CANDIDATES,CANDIDATES,build_blocks,frozen,assemble_graph,fold_view_entries
from scripts.hunet_opt.policy import build_policy
from scripts.hunet_opt.layout import gn_layout,timing_layout,validate_layout
def canonical_bytes(value):
    import json
    return (json.dumps(value,sort_keys=True,separators=(',',':'))+'\n').encode()
def op_names():
    root=Path(__file__).resolve().parents[2]
    result={int(v):n for n,v in re.findall(r'\b(HUNET_\w+)\s*=\s*(\d+)',(root/'vendor/hunet/v5/include/hunet_job.h').read_text())}
    for relative,names in [('include/vxl/hunet_fused_wire.h',('VXL_FUSED_OP',)),('include/vxl/hunet_opt_wire.h',('VXL_OPT_GN_NORM_OP','VXL_OPT_GRAPH_BLOCK_OP'))]:
        text=(root/relative).read_text()
        for name in names:
            match=re.search(r'^#define\s+'+name+r'\s+(\d+)u?$',text,re.M)
            if not match:raise ValueError('ENUM_SOURCE_MISSING:'+name)
            value=int(match.group(1))
            if value in result:raise ValueError('ENUM_DUPLICATE')
            result[value]=name
    return result
def _compile(candidate):
    contract,schedule,layout=frozen();entries=build_blocks(contract['jobs'],schedule['cpu'],candidate)
    view_report=None
    if candidate in ('G3_VIEWS','COMBINED'):entries,view_report=fold_view_entries(entries)
    slots=assemble_graph(entries)
    root=Path(__file__).resolve().parents[2]
    model=json.loads((root/'contracts/hunet_v5.json').read_text())
    regions=model['graph']['layout'];matrix=(root/'generated/hunet_fused/descriptors.bin').read_bytes()
    for entry in entries:
        words=entry['words'];addresses=[p for n in entry['nodes'] for p in n[4:12]] if entry['op'] in (16,17) else words[8:16] if entry['op']!=15 else words[8:12]
        spans=[]
        for address in sorted(set(addresses)-{0}):
            found=next((r for r in regions if r['start']<=address<r['start']+r['bytes']),None)
            if found:spans.append(dict(address=address,limit=found['start']+found['bytes'],region=found['name'],scope='reserved_span_not_accessed_byte_count'))
            elif 0x80800000<=address<0x80800000+23685056:spans.append(dict(address=address,limit=0x80800000+23685056,region='actual_weights',scope='reserved_span_not_accessed_byte_count'))
            else:raise ValueError('UNPROVED_FROZEN_ADDRESS:%x'%address)
        entry['input_parameter_ranges']=spans
        if entry['op']==15:
            start=words[14]-layout['descriptors']['start'];entry['descriptor_sha256']=hashlib.sha256(matrix[start:start+words[7]*64]).hexdigest()
        else:entry['descriptor_sha256']=slots[entry['block_id']-1]['sha256'] if entry['block_id'] else None
    result=dict(schema=2,scope='STATIC_PROGRAM10_CATALOG_NOT_GPU',program_id=10,candidate=candidate,
                gpu_jobs=sum(e['kind']=='GPU' for e in entries),cpu_jobs=sum(e['kind']=='CPU' for e in entries),logical_jobs=1023,stages=100,entries=entries,
                logical_sha256=schedule['logical_sha256'],aux_sha256=schedule['aux_sha256'],hardware_profile=contract['baseline']['profile'],
                layout=gn_layout(),graph_slots=slots,matrix_layout=layout,
                tile_n=build_policy(candidate)['tile_n'])
    result['layout']['execution_policy']=build_policy(candidate)
    result['layout']['timing']=timing_layout()
    result['layout']['metadata_reservation']=dict(start=0x84000000,end=0x84142000,containing_object=[0x80800000,0x84800000],actual_weights_end=0x80800000+23685056,trace_capacity=262144)
    if view_report:
        result['view_report']=view_report
        result['layout']['view_table_sha256']=hashlib.sha256(canonical_bytes([dict(block_id=e['block_id'],views=e['view_sources'],virtual_nodes=e['virtual_nodes']) for e in entries if e.get('view_sources')])).hexdigest()
    result['source_provenance']=dict(hardware='current selected W4; historical pre-W4 workload validated against archived contract',old_header_manifest='only known 4fd50b8 unused attribute accepted after exact reverse-byte check; old manifest unchanged')
    names=op_names();result['operator_coverage']=[]
    for stage in range(100):
        old=[j for j in contract['jobs'] if j[30]==stage];producers=[e for e in entries if e['first_stage']<=stage<=e['last_stage']]
        result['operator_coverage'].append(dict(stage=stage,original_ops=[dict(op=op,name=names[op],jobs=sum(j[3]==op for j in old)) for op in sorted({j[3] for j in old})],physical_entries=[dict(first=e['first'],last=e['last'],op=e['op'],name=names[e['op']]) for e in producers],materialized=not any(stage in e['virtual_stages'] for e in producers),gpu_cycles=None))
    if candidate.startswith('G4') or candidate=='COMBINED':
        result['scalar_policy']=dict(chunk=1 if candidate=='G4_C1' else 16 if candidate=='G4_C16' else 4,allowed_chunks=[1,4,16],gap_reorder=False,performance_rank='NOT_RUN')
    return result
def validate_catalog(catalog):
    expected=_compile(catalog.get('candidate',''))
    if catalog!=expected:raise ValueError('CATALOG_NOT_FROZEN_CANONICAL')
    cursor=1;stages=set();cpu=[]
    validate_layout(catalog['layout'])
    for e in catalog['entries']:
        if e['first']!=cursor or e['last']<cursor or e['last']>1023:raise ValueError('LOGICAL_COVERAGE')
        cursor=e['last']+1;stages.update(range(e['first_stage'],e['last_stage']+1))
        if e['kind']=='CPU':cpu.append((e['first_stage'],e['first']))
    if cursor!=1024 or stages!=set(range(100)) or len(cpu)!=13:raise ValueError('GRAPH_COVERAGE')
    if catalog['gpu_jobs']!=expected['gpu_jobs'] or catalog['cpu_jobs']!=13:raise ValueError('BASE_PHYSICAL_COUNTS')
    from scripts.hunet_opt.contract import validate_graph
    for e in catalog['entries']:
        if e['block_id']:validate_graph(struct.pack('<32I',*e['words']),bytes.fromhex(catalog['graph_slots'][e['block_id']-1]['hex']))
    return None
def compile_catalog(candidate='G1'):
    result=_compile(candidate);validate_catalog(result);return result
def validate_submission(catalog,words,next_sequence,descriptor=None):
    for e in catalog['entries']:
        if e['kind']=='GPU' and e['words']==list(words) and e['first']==next_sequence:
            if e['block_id'] and descriptor is not None and descriptor!=bytes.fromhex(catalog['graph_slots'][e['block_id']-1]['hex']):raise ValueError('DESCRIPTOR_NOT_CANONICAL')
            return copy.deepcopy(e)
    raise ValueError('SUBMISSION_NOT_EXACT_CATALOG')
