"""Content-addressable generated artifacts, excluding timestamps/generation."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from scripts.hunet_opt.catalog import BASE_CANDIDATES,CANDIDATES,compile_catalog,validate_catalog,canonical_bytes
ROOT=Path(__file__).resolve().parents[2]
def cbytes(data):return '{'+','.join('0x%02x'%b for b in data)+'}'
def emit_artifacts(catalog,out):
    validate_catalog(catalog);out=Path(out);out.mkdir(parents=True,exist_ok=True)
    gpu=[e for e in catalog['entries'] if e['kind']=='GPU']
    job_bytes=b''.join(struct.pack('<32I',*e['words']) for e in gpu)
    graph=b''.join(bytes.fromhex(s['hex'])+bytes(4096-s['bytes']) for s in catalog['graph_slots']);matrix=(ROOT/'generated/hunet_fused/descriptors.bin').read_bytes()
    files={'jobs.bin':job_bytes,'catalog.json':canonical_bytes(catalog),'layout.json':canonical_bytes(catalog['layout']),
           'graph-descriptors.bin':graph,'matrix-descriptors.bin':matrix}
    metas=[[e[k] for k in ('first','last','first_stage','last_stage','op','block_id','owner_tiles')]+[len(e['materialized_stages'])] for e in gpu]
    cpu=[[e['first'],e['first_stage']] for e in catalog['entries'] if e['kind']=='CPU']
    lines=['#ifndef VXL_OPT_CATALOG_GENERATED_H','#define VXL_OPT_CATALOG_GENERATED_H',
           'static const vxl_u32 vxl_opt_jobs[%d][32]={'%len(gpu)]
    lines+=['{'+','.join('0x%08xu'%v for v in e['words'])+'},' for e in gpu]
    lines+=['};','static const struct vxl_opt_meta vxl_opt_metadata[%d]={'%len(metas)]
    lines+=['{'+','.join(str(v)+'u' for v in row)+'},' for row in metas]
    lines+=['};','static const vxl_u32 vxl_opt_cpu[%d][2]={'%len(cpu)]+['{%du,%du},'%tuple(row) for row in cpu]+['};']
    lines+=['static const vxl_u8 vxl_opt_matrix_data[%d]='%len(matrix)+cbytes(matrix)+';']
    if graph:lines+=['static const vxl_u8 vxl_opt_graph_data[%d]='%len(graph)+cbytes(graph)+';']
    lines+=['static const struct vxl_opt_catalog vxl_opt_selected_catalog={',
            '.gpu_jobs=%du,.cpu_jobs=13u,.logical_jobs=1023u,.tile_n=%du,'%(len(gpu),catalog['tile_n']),
            '.runtime_flags=%du,'%((1 if catalog['layout']['execution_policy']['load_fastwait'] else 0)|(2 if catalog['layout']['execution_policy']['control_page'] else 0)|(4 if catalog['layout']['execution_policy']['parallel_gn'] else 0)|(8 if catalog['layout']['execution_policy']['internal_timing'] else 0)),
            '.jobs=vxl_opt_jobs,.metadata=vxl_opt_metadata,.cpu=vxl_opt_cpu,',
            '.matrix_data=vxl_opt_matrix_data,.matrix_bytes=%du,'%len(matrix),
            ('.graph_data=vxl_opt_graph_data,.graph_bytes=%du,'%len(graph)) if graph else '.graph_data=0,.graph_bytes=0,',
            '.jobs_sha='+cbytes(hashlib.sha256(job_bytes).digest())+',',
            '.graph_sha='+cbytes(hashlib.sha256(graph).digest())+',',
            '.matrix_sha='+cbytes(hashlib.sha256(matrix).digest())+',',
            '.descriptors_sha='+cbytes(hashlib.sha256(matrix+graph).digest())+',',
            '.layout_sha='+cbytes(hashlib.sha256(files['layout.json']).digest())+',',
            '};','#endif']
    files['catalog.h']=('\n'.join(lines)+'\n').encode()
    views=[];virtual=[];graph_entries=[e for e in gpu if e['op']==17]
    if graph_entries:
        view_lines=['#pragma once','#include "../../../device/hunet_opt_views.h"']
        graph_lines=['#pragma once','#include "../../../device/hunet_opt_graph.h"']
        for entry in graph_entries:
            for source in entry['view_sources']:
                name='opt_segments_%d_%d'%(entry['block_id'],source['node_index'])
                view_lines+=['static const OptViewSegment %s[]={'%name]
                for segment in source['segments']:
                    view_lines+=['{'+','.join(str(segment[k]) for k in ('dst_first','count','source_gpu','source_first','source_ci','source_h','source_w','dy','dx'))+'},']
                view_lines+=['};']
                views.append('{%du,%du,%du,%du,%du,%du,%s},'%(entry['block_id'],source['node_index'],source['channels'],source['height'],source['width'],len(source['segments']),name))
            virtual.extend((entry['block_id'],i) for i in entry['virtual_nodes'])
            raw=bytes.fromhex(catalog['graph_slots'][entry['block_id']-1]['hex'])
            graph_lines+=['static const uint8_t opt_graph_bytes_%d[%d]='%(entry['block_id'],len(raw))+cbytes(raw)+';']
        view_lines+=['static const OptView opt_views[]={']+views+['};',
          'static inline const OptView*opt_bound_view(uint32_t block,uint32_t node){for(const auto&v:opt_views)if(v.block_id==block&&v.node_index==node)return &v;return nullptr;}',
          'static inline bool opt_virtual_node(uint32_t block,uint32_t node){return '+ '||'.join('(block==%du&&node==%du)'%v for v in virtual)+';}']
        graph_lines+=['static const OptBoundGraph opt_graphs[]={']
        graph_lines+=['{%du,%du,{'%(e['block_id'],e['words'][10])+','.join('0x%08xu'%w for w in e['words'])+'},opt_graph_bytes_%d},'%e['block_id'] for e in graph_entries]
        graph_lines+=['};','static inline const OptBoundGraph*opt_bound_graph(uint32_t block){for(const auto&g:opt_graphs)if(g.block_id==block)return &g;return nullptr;}']
        matrix_stages=[e['words'] for e in compile_catalog('G1')['entries'] if e['op']==15]
        graph_lines+=['static const HunetFusedStageV1 opt_matrix_stages[]={']+['{'+','.join('0x%08xu'%w for w in row)+'},' for row in matrix_stages]+['};',
          'static inline const HunetFusedStageV1*opt_bound_matrix(uint32_t stage){for(const auto&s:opt_matrix_stages)if(s.stage_id==stage)return &s;return nullptr;}']
        files['views.h']=('\n'.join(view_lines)+'\n').encode();files['device_graph.h']=('\n'.join(graph_lines)+'\n').encode()

    manifest=dict(scope='GENERATED_STATIC_NOT_GPU',candidate=catalog['candidate'],program_id=10,gpu_jobs=len(gpu),cpu_jobs=13,logical_jobs=1023,stages=100,gpu_cycles=None,files={name:dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest()) for name,data in files.items()})
    files['manifest.json']=canonical_bytes(manifest)
    for name,data in files.items():(out/name).write_bytes(data)
    return manifest
def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--candidate',choices=CANDIDATES,default='G1');a=p.parse_args()
    emit_artifacts(compile_catalog(a.candidate),a.out);print('PROGRAM10_CATALOG_GENERATED_STATIC_ONLY')
if __name__=='__main__':main()
