"""Deterministic driver/kernel artifacts, with distinct content identities."""
import argparse
import hashlib
import json
import pathlib
import struct
from catalog import ROOT,freeze_catalog,compile_schedule
from layout import compile_layout,validate_layout


def canonical_bytes(value):
    return (json.dumps(value,sort_keys=True,separators=(',',':'))+'\n').encode('utf-8')


def stage_words(stage,layout):
    return ([0x53554648,1,128,15,stage['first'],stage['last'],stage['stage_id'],len(stage['descriptors'])]+
            [stage['input_gpu'],stage['weights_gpu'],stage['bias_gpu'],stage['output_gpu'],
             layout['packed_b']['start'],stage['packed_b_bytes'],
             layout['descriptors']['start']+stage['descriptor_index']*64,layout['owner_trace']['start']]+
            [stage['pack_grid_rows'],stage['gemm_grid_tiles']]+stage['shape']+
            [stage['flags'],stage['gemm_grid_tiles'],1,1,0])


def c_rows(rows):
    return ['{'+','.join('0x%08xu'%v for v in row)+'},' for row in rows]


def emit_artifacts(layout,schedule,out):
    validate_layout(layout)
    out=pathlib.Path(out);out.mkdir(parents=True,exist_ok=True)
    by_stage={x['stage_id']:x for x in layout['matrix_stages']}
    jobs=[];meta=[];descriptors=[]
    for stage in layout['matrix_stages']:descriptors.extend(stage['descriptors'])
    for entry in schedule['gpu']:
        if entry['op']==15:
            stage=by_stage[entry['stage']]
            words=stage_words(stage,layout)
            metadata=[entry['first'],entry['last'],entry['stage'],15,
                      stage['gemm_grid_tiles'],stage['pack_grid_rows'],stage['gemm_grid_tiles']]
        else:
            words=entry['logical_job']
            metadata=[entry['first'],entry['last'],entry['stage'],entry['op'],0,0,0]
        if len(words)!=32:raise ValueError('STAGE_WIRE_WORD_COUNT')
        jobs.append(words);meta.append(metadata)
    files={
        'jobs.bin':b''.join(struct.pack('<32I',*row) for row in jobs),
        'descriptors.bin':b''.join(struct.pack('<16I',*row) for row in descriptors),
        'layout.json':canonical_bytes(layout),
        'schedule.json':canonical_bytes(schedule),
    }
    files['catalog.h']=('\n'.join(['#ifndef VXL_FUSED_CATALOG_GENERATED_H','#define VXL_FUSED_CATALOG_GENERATED_H',
        'static const vxl_u32 vxl_fused_jobs[103][32]={']+c_rows(jobs)+['};',
        'static const struct vxl_fused_meta vxl_fused_metadata[103]={']+c_rows(meta)+['};','#endif'])+'\n').encode()
    files['descriptors.h']=('\n'.join(['#ifndef VXL_FUSED_DESCRIPTORS_GENERATED_H','#define VXL_FUSED_DESCRIPTORS_GENERATED_H',
        'static const HunetFusedDescriptorV1 vxl_fused_descriptors[314]={']+
        ['{'+','.join('0x%08xu'%v for v in row[:13])+',{0,0,0}},' for row in descriptors]+['};','#endif'])+'\n').encode()
    constants=[]
    for name in ('packed_b','descriptors','owner_trace'):
        for key in ('start','bytes'):
            constants.append('#define VXL_FUSED_%s_%s 0x%08xu'%(name.upper(),key.upper(),layout[name][key]))
    files['layout.h']=('\n'.join(['#ifndef VXL_FUSED_LAYOUT_GENERATED_H','#define VXL_FUSED_LAYOUT_GENERATED_H']+constants+
        ['static const vxl_u32 vxl_fused_guards[3] __attribute__((unused))={'+','.join('0x%08xu'%v for v in layout['guards'])+'};',
         '#endif'])+'\n').encode()
    identities={'contract':(ROOT/'contracts/hunet_fused_v1.json').read_bytes(),
                'jobs':files['jobs.bin'],'descriptors':files['descriptors.bin'],'layout':files['layout.json']}
    files['identity.h']=('\n'.join(['#ifndef VXL_FUSED_IDENTITY_GENERATED_H','#define VXL_FUSED_IDENTITY_GENERATED_H']+
        ['static const vxl_u8 vxl_fused_%s_sha256[32]={%s};'%(name,','.join('0x%02x'%b for b in hashlib.sha256(data).digest())) for name,data in identities.items()]+['#endif'])+'\n').encode()
    for name,data in files.items():(out/name).write_bytes(data)
    manifest=dict(scope='GENERATED_STATIC_FUSION_ARTIFACTS_NOT_GPU_PASS',program_id=9,
                  layout_sha256=hashlib.sha256(files['layout.json']).hexdigest(),
                  jobs_sha256=hashlib.sha256(files['jobs.bin']).hexdigest(),
                  descriptors_sha256=hashlib.sha256(files['descriptors.bin']).hexdigest(),
                  files={name:hashlib.sha256(data).hexdigest() for name,data in files.items()})
    (out/'manifest.json').write_bytes(canonical_bytes(manifest))
    return manifest


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--out',type=pathlib.Path,required=True)
    args=parser.parse_args();contract=freeze_catalog(ROOT);schedule=compile_schedule(contract)
    emit_artifacts(compile_layout(contract,schedule),schedule,args.out)
    print('FUSED_GENERATED_LAYOUT_AND_CATALOG_PASS_STATIC_ONLY')
