"""Frozen W16A16 weights to77 SSA phases, metadata only; no GPU execution.

No historical GPU/CPU address enters the graph. Norm coefficient concatenation
preserves original byte order. All arithmetic remains in the audited library.
"""
import hashlib,json,math,pathlib,struct,sys
from scripts.mosaic.assets import load_assets
from scripts.unified.mosaic_program import selected_directory,resolve_variant,requires_fusion
from scripts.unified.mosaic_weight_layout import repack_weights,DERIVED_LAYOUT_NAME
ROOT=pathlib.Path(__file__).resolve().parents[2]
WEIGHTS_SHA='f10ffc08bf9a2f89fb6561999938c24ddaebb56af255875a42652fde65acf973'
NAMES={1:'QuantI16.v1',2:'ConvI16I48.v1',3:'NormStats.v3',4:'NormApply.v3',5:'Relu.v1',6:'Add.v1'}
def wire(x):return json.dumps(x,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode()
def sha(x):return hashlib.sha256(x).hexdigest()
def cooperative_workspace_policy(profiling_options):
 if type(profiling_options) is not int or profiling_options not in (0,1):raise ValueError('COOPERATIVE_PROFILE_OPTIONS_BIT0_ONLY')
 return dict(version=1,abi=3,layout_version=1,profile_wire_version=2,prefix_bytes=64,conv_raw_bytes=384,core_profile_bytes=256,schedule_version=1,weight_layout_version=1,profiling_options=profiling_options)

def program_workspace_policy(variant,profiling_options):
 """Keep ABI/schedule/weight contracts when selecting either reuse release."""
 variant=resolve_variant(variant);reuse=variant in ('reuse-v7','reuse-v8');modern=variant=='cooperative-v6' or reuse
 policy=cooperative_workspace_policy(profiling_options)
 if reuse:policy.update(abi=4,schedule_version=2,weight_layout_version=2)
 if not modern and profiling_options:raise ValueError('PROFILING_OPTIONS_REQUIRE_MODERN_WORK_ABI')
 return policy

def implementation_source_rows(root,*,variant=None):
 """Exact host sources and active selection inputs used in the package key."""
 root=pathlib.Path(root);variant=resolve_variant(variant);fusion=requires_fusion(variant);reuse=variant in ('reuse-v7','reuse-v8')
 sources=['include/vxunified/mosaic_request.hpp','unified/mosaic_request.cpp','include/vxnn/mosaic_lowering.hpp','nnrt/ops/mosaic_lowering.cpp','include/vxnn/mosaic_registry.hpp','include/vxnn/model_mosaic.hpp','nnrt/ops/mosaic.cpp','nnrt/models/mosaic.cpp','nnrt/models/mosaic_plan.cpp','device/vxnn/mosaic_wire.h','device/vxnn/mosaic_wire.cpp','device/vxnn/mosaic_wire_ro64.cpp','include/vxnn/mosaic_ro64_program.hpp','include/vxnn/request_input.hpp','scripts/unified/mosaic_program.py','contracts/nnrt/ops-mosaic-v1.json','scripts/unified/generate_mosaic_package.py','core/hunet_sha256.c']
 sources += ['scripts/unified/program_identity.py','include/vxnn/mosaic_program_family.hpp','include/vxnn/mosaic_coop_program.hpp','device/vxnn/mosaic_wire_coop_v3.h','device/vxnn/mosaic_wire_coop_v3.cpp','device/vxnn/mosaic_work_layout_v1.h']
 # Both headers are unconditional C++ dependencies of mosaic_program_family.hpp.
 sources += ['include/vxnn/mosaic_reuse_program.hpp','include/vxnn/mosaic_reuse_v8_program.hpp']
 if reuse:
  sources += ['scripts/unified/mosaic_schema_reuse_v4.py','scripts/unified/mosaic_weight_layout.py','include/vxnn/mosaic_weight_layout.hpp','device/vxnn/mosaic_wire_reuse_v4.h','device/vxnn/mosaic_wire_reuse_v4.cpp']
  sources += ['scripts/unified/mosaic_reuse_v8_program.py' if variant=='reuse-v8' else 'scripts/unified/mosaic_reuse_program.py']
 if fusion:sources += ['scripts/unified/fuse_mosaic_pointwise.py','device/vxnn/mosaic_wire_fused_v1.cpp','include/vxnn/mosaic_fused_program.hpp']
 for directory in ['nnrt/package','nnrt/graph','nnrt/memory']:
  sources += [str(p.relative_to(root)).replace('\\','/') for p in sorted((root/directory).glob('*.cpp'))]
 sources += ['include/vxnn/'+n+'.hpp' for n in ['types','package','graph','arena','arena_guard','arena_storage']]
 return [dict(path=n,sha256=sha((root/n).read_bytes())) for n in sorted(set(sources))]

def generate(weight_path,manifest_path,input_path,width,height,budget,out,provider_flag=1,*,pointwise_budget=None,fusion=None,cooperative=None,profiling_options=0,variant=None):
 """The positional budget keeps the legacy all-op contract by default.

 An explicit pointwise budget creates a versioned split policy; the original
 budget then applies to convolution only. Admission is metadata, not a claim
 that either budget has passed a device watchdog or a performance target.
 """
 if type(width) is not int or type(height) is not int or width<8 or width>1280 or height<8 or height>960 or width%4 or height%4 or type(budget) is not int or not 1<=budget<=4096:raise ValueError('BOUNDED_GEOMETRY_BATCH')
 if pointwise_budget is not None and (type(pointwise_budget) is not int or not 1<=pointwise_budget<=4096):raise ValueError('BOUNDED_POINTWISE_BATCH')
 variant=resolve_variant(variant,fused=fusion,cooperative=cooperative);fusion=requires_fusion(variant);cooperative=variant=='cooperative-v6';reuse=variant in ('reuse-v7','reuse-v8');modern=cooperative or reuse
 if fusion and pointwise_budget is None:raise ValueError('EXPLICIT_FUSED_SPLIT_BATCH_POLICY_REQUIRED')
 work_policy=program_workspace_policy(variant,profiling_options)
 weights=pathlib.Path(weight_path).read_bytes();manifest=json.loads(pathlib.Path(manifest_path).read_bytes());layers=load_assets(weights,manifest)
 if sha(weights)!=WEIGHTS_SHA:raise ValueError('FROZEN_W16A16_WEIGHTS')
 input_raw=pathlib.Path(input_path).read_bytes()
 if len(input_raw)!=3*width*height*4 or any(not math.isfinite(v) for(v,)in struct.iter_unpack('<f',input_raw)):raise ValueError('EXACT_FINITE_CHW_FP32_INPUT')
 family_dir=selected_directory(ROOT,variant=variant);family=json.loads((family_dir/'family.json').read_bytes())
 for filename,key in [('code.bin','code_sha256'),('rodata.bin','rodata_sha256'),('args-schema.bin','schema_sha256')]:
  if sha((family_dir/filename).read_bytes())!=family[key]:raise ValueError('PROGRAM_IDENTITY')
 rows=implementation_source_rows(ROOT,variant=variant);impl=sha(wire(rows));payload={};roles={};tensors=[];nodes=[];audit=[]
 provider=next(p for p in json.loads((ROOT/'contracts/unified/providers.json').read_bytes())['providers'] if type(provider_flag) is int and provider_flag in (1,4) and p['provider_flags']==provider_flag)
 def asset(name,raw,role):payload[name]=raw;roles[name]=role;return name
 def tensor(dtype,layout,shape,source=None):
  t=dict(id=len(tensors)+1,dtype=dtype,layout=layout,shape=shape,domain='PL',access=1 if source else 3,alignment=64,constant=bool(source))
  if source:t['asset']=source
  tensors.append(t);return t['id']
 def constant(index,suffix,dtype,layout,shape,raw):return tensor(dtype,layout,shape,asset('layer-%02d-%s.bin'%(index,suffix),raw,'weights'))
 derivations=[]
 first=tensor('FP32','CHW_F32',[3,height,width]);current=first;saved=0;ci=3;h=height;w=width;total_jobs=0
 graph_spec=json.loads((ROOT/'config/mosaic/model-v1.json').read_bytes())['layers']
 for index,l in enumerate(layers):
  g=graph_spec[index];co=l['co'];k=l['k'];s=g['stride'];up=g['upsample'];kp=(ci*k*k+31)//32*32;oh=(h*up+s-1)//s;ow=(w*up+s-1)//s
  if (l['ci'],l['co'],l['k'],l['norm'])!=(ci,g['co'],g['k'],int(g['norm'])):raise ValueError('FROZEN_MODEL_GEOMETRY')
  if reuse:
   derived=repack_weights(l['q16'],co,kp);raw=derived.pop('derived_bytes');filename='layer-%02d-q16-layout2.bin'%index
   derived.update(layer=index,asset=filename,source_offset_bytes=struct.unpack_from('<Q',weights,256+index*384+64+8)[0]);derivations.append(derived)
   weight=tensor('I16',DERIVED_LAYOUT_NAME,derived['derived_shape'],asset(filename,raw,'weights'))
  else:weight=constant(index,'q16','I16','CO_KPAD_I16',[co,kp],l['q16'])
  conv_coeff=constant(index,'coeff','FP32','MOSAIC_COEFF_F32',[co*2],l['coeff'])
  norm_coeff=constant(index,'norm','FP32','MOSAIC_COEFF_F32',[co*4],l['coeff']+l['norm_gamma']+l['norm_beta']) if l['norm'] else None
  sa=struct.unpack('<I',l['sA'])[0];params_shape=[ci,co,h,w,k,s,up,kp];quant=0;fp=0;stats=0
  sequence=[1,2]+([3,4] if l['norm'] else [])+([5] if g['relu'] else [])+([6] if g['residual'] else [])
  for op in sequence:
   flags=(1 if g['save'] else 0) if op==1 else 0 if op==2 else 2;ins=[];outs=[]
   if op==1:
    ins=[current];quant=tensor('I16','CHW_I16',[ci,h,w]);outs=[quant]
    if flags:saved=tensor('FP32','CHW_F32',[ci,h,w]);outs.append(saved)
   if op==2:ins=[quant,weight,conv_coeff];fp=tensor('FP32','CHW_F32',[co,oh,ow]);outs=[fp]
   if op==3:ins=[fp];stats=tensor('FP32','CO_STATS3_F32',[co,3]);outs=[stats]
   if op==4:ins=[fp,norm_coeff,stats];fp=tensor('FP32','CHW_F32',[co,oh,ow]);outs=[fp]
   if op==5:ins=[fp];fp=tensor('FP32','CHW_F32',[co,oh,ow]);outs=[fp]
   if op==6:
    if not saved:raise ValueError('RESIDUAL_PRODUCER')
    ins=[fp,saved];fp=tensor('FP32','CHW_F32',[co,oh,ow]);outs=[fp];saved=0
   node_budget=budget if pointwise_budget is None or op==2 else pointwise_budget
   items=(ci*h*w+255)//256 if op==1 else ((co+7)//8)*((oh*ow+7)//8) if op==2 else co if op==3 else (co*oh*ow+255)//256;jobs=(items+node_budget-1)//node_budget;total_jobs+=jobs
   name='Mosaic.'+NAMES[op];key=sha(('VXNN_MOSAIC_OP:1:'+name+':'+impl+':'+family['code_sha256']+':'+family['rodata_sha256']+':'+family['schema_sha256']).encode())
   nodes.append(dict(id=len(nodes)+1,operator=dict(name=name,version='1.0.0',implementation_sha=key),inputs=ins,outputs=outs,parameters=dict(shape=params_shape,flags=flags,scale_bits=sa if op<3 else 0,budget=node_budget),job_count=jobs,evidence_bytes=4096+jobs*512,program_requirements=[family['code_sha256']]))
   audit.append(dict(phase=len(audit),layer=index,opcode=op,items=items,jobs=jobs))
   if pointwise_budget is not None:audit[-1].update(budget=node_budget,tail_items=items-(jobs-1)*node_budget)
  current=fp;ci=co;h=oh;w=ow
 if len(nodes)!=77 or h!=height or w!=width or saved:raise ValueError('FULL16_LAYER_GEOMETRY')
 if fusion:
  from scripts.unified.fuse_mosaic_pointwise import fuse
  graph,audit=fuse(dict(schema=1,tensors=tensors,nodes=nodes,inputs=[first],outputs=[current],shape_profiles=[]),audit,pointwise_budget,dict(implementation_sha256=impl,code_sha256=family['code_sha256'],readonly_sha256=family['rodata_sha256'],schema_sha256=family['schema_sha256']))
  nodes=graph['nodes'];tensors=graph['tensors'];total_jobs=sum(n['job_count'] for n in nodes)
 if modern:
  for node in nodes:
   node['parameters'].update(work_layout_version=1,profiling_options=profiling_options)
   node['evidence_bytes']=4096+node['job_count']*(1536 if profiling_options else 512)
 if reuse:
  for node in nodes:node['parameters'].update(schedule_version=2,weight_layout_version=2)
 evidence=sum(n['evidence_bytes'] for n in nodes)
 if evidence>1<<30:raise ValueError('EVIDENCE_BUDGET_REQUIRES_LARGER_BOUNDED_BATCH')
 graph=dict(schema=1,tensors=tensors,nodes=nodes,inputs=[first],outputs=[current],shape_profiles=[])
 programs=dict(schema=1,hardware_contract=provider['hardware_contract'],bit=provider['bit_sha256'],xsa=provider['xsa_sha256'],compatible=sha(provider['compatible'].encode()),programs=[dict(namespace=1,profile=1,program=65568,code=family['code_sha256'],schema=family['schema_sha256'])])
 thresholds=wire(dict(layer_abs=0.02,layer_rel=0.0002,final_max_abs=0.1,final_rmse=0.01))
 formal=(ROOT/'docs/mosaic/FORMAL_COMPAT_V3_20261020.json').read_bytes();history=(ROOT/'docs/mosaic/NORM_COMPAT_CANDIDATES_20261020.json').read_bytes()
 numeric=dict(schema=1,reference_sha=sha(formal),threshold_sha=sha(thresholds),semantics='MOSAIC_W16A16_EXACT48_FP32_NORM_V3_FROZEN_THRESHOLDS',known_failures=['LEGACY_FIRST_CONV4096_WATCHDOG_RETAINED','LEGACY_NORM_CANDIDATES_RETAINED'])
 binding=dict(schema=1,code_sha256=family['code_sha256'],readonly_sha256=family['rodata_sha256'],schema_sha256=family['schema_sha256'],implementation_sha256=impl,source_rows=rows,hardware=provider,width=width,height=height,budget=budget,weights_sha256=WEIGHTS_SHA,nodes=audit,scope='METADATA_NOT_INFERENCE_OR_GPU',gpu_raw_cycles=None)
 if modern:binding['workspace_policy']=work_policy
 if reuse:
  derived_meta=wire(dict(version=1,source_asset='weights-original.ms8',source_sha256=WEIGHTS_SHA,original_layout_id=1,layout_id=2,layout=DERIVED_LAYOUT_NAME,layers=derivations));asset('weights-layout2.json',derived_meta,'weight-derivation')
  binding['weight_layout_policy']=dict(version=1,original_layout_id=1,layout_id=2,layout=DERIVED_LAYOUT_NAME,metadata_asset='weights-layout2.json',metadata_sha256=sha(derived_meta),source_asset='weights-original.ms8',source_sha256=WEIGHTS_SHA)
 if fusion:binding['fusion_policy']=dict(version=1,norm_stats=15,saved_residuals=5,precision='EXACT_OLD_FP32_STORES_RNE_INT16_V1')
 if pointwise_budget is not None:binding['batch_policy']=dict(version=1,convolution_budget=budget,pointwise_budget=pointwise_budget)
 for name,data,role in [('weights-original.ms8',weights,'provenance-source'),('input.f32',input_raw,'input'),('formal-history.json',formal,'historical-numerics'),('numeric-history.json',history,'historical-failures'),('thresholds.json',thresholds,'thresholds')]:asset(name,data,role)
 for name,data,role in [('graph.json',graph,'graph'),('program-requirements.json',programs,'programs'),('numeric-contract.json',numeric,'numeric'),('binding.json',binding,'provenance')]:asset(name,wire(data),role)
 # Historical JSON contains FP32 measurements/thresholds. Retain raw bytes as
 # binary assets rather than weakening canonical graph metadata (integer-only).
 metadata={'graph.json','program-requirements.json','numeric-contract.json','binding.json'}
 if reuse:metadata.add('weights-layout2.json')
 manifest=dict(schema=1,max_jobs=total_jobs,max_evidence_bytes=evidence,assets=[dict(path=name,bytes=len(raw),sha256=sha(raw),type='json' if name in metadata else 'binary',role=roles[name]) for name,raw in sorted(payload.items())]);manifest['model_id']=sha(wire(manifest));payload['manifest.json']=wire(manifest)
 out=pathlib.Path(out);out.mkdir(parents=True,exist_ok=False)
 for name,raw in payload.items():(out/name).write_bytes(raw)
 return dict(model_id=manifest['model_id'],nodes=len(nodes),tensors=len(tensors),jobs=total_jobs,bytes=sum(map(len,payload.values())),evidence_bound=evidence,gpu_raw_cycles=None)
if __name__=='__main__':print(json.dumps(generate(sys.argv[1],sys.argv[2],sys.argv[3],int(sys.argv[4]),int(sys.argv[5]),int(sys.argv[6]),sys.argv[7])))
