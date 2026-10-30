"""Fail-closed v4 raw-run audit. Offline models cannot become real-board PASS."""
import hashlib,importlib.util,json,struct
from pathlib import Path
from scripts.hunet_opt.catalog import validate_catalog
from scripts.hunet_opt.summary import summarize,expected_owner_counts,gn_internal,internal_cycles,load_breakdown,digest_breakdown,kernel_digest_breakdown
ROOT=Path(__file__).resolve().parents[2]
def need(condition,reason):
    if not condition:raise ValueError(reason)
def sha(raw):return hashlib.sha256(raw).hexdigest()
def owned(root,name,record):
    need(isinstance(name,str) and not name.startswith('/') and '\\' not in name and all(x not in ('','..','.') for x in name.split('/')),'PATH')
    p=Path(root)
    for part in name.split('/'):
        p=p/part;need(not p.is_symlink(),'SYMLINK')
    b=p.read_bytes();need(len(b)==record['bytes'] and sha(b)==record['sha256'],'FILE_IDENTITY:'+name);return b
def audit_run(run,release):
    result=dict(scope='UNVERIFIED',execution='FAIL',formal_eligible=False,publish_performance=False,h0='NOT_EVALUATED',h1='NOT_RUN')
    try:
        from scripts.hunet_opt.package_release import verify_release
        run,release=Path(run),Path(release);verified=verify_release(release)
        need(verified.get('integrity')=='PASS' and verified.get('program_id')==10,'RELEASE_IDENTITY')
        catalog=json.loads((release/'catalog.json').read_bytes());validate_catalog(catalog)
        m=json.loads((run/'run.json').read_bytes());gate=json.loads((run.parent/'readonly-preflight.json').read_bytes())
        need(m.get('scope')=='HUNET_OPT_LINUX_V2' and m.get('profile')==4 and m.get('program_id')==10 and m.get('error')==0,'TARGET_RAW_IDENTITY')
        need(gate.get('integrity')=='PASS' and gate.get('scope')=='HW4_OPT_READONLY_PREFLIGHT','TRUSTED_PREFLIGHT')
        for k in ('boot_id','build_id','kernel'):need(m.get(k) and m[k]==gate.get(k),'BOOT_EPOCH:'+k)
        for k in ('source_digest','binding_sha256','bit_sha256','xsa_sha256'):need(verified.get(k) and verified[k]==gate.get(k),'RELEASE_PREFLIGHT:'+k)
        need(gate.get('expected_run_id')==m['run_id'] and len(bytes.fromhex(m['run_id']))==16 and int(m['run_id'],16),'RUN_ID')
        need(type(m.get('generation')) is int and m['generation']>=gate.get('minimum_generation',1) and m.get('roi_index')==0,'GENERATION_ROI')
        limits={1:1,2:3,3:20,4:256};kind=m.get('sample_kind');index=m.get('sample_index')
        need(kind in limits and type(index) is int and 0<=index<limits[kind] and ((m.get('mode')=='measured' and kind!=4) or (m.get('mode')!='measured' and kind==4)),'SAMPLE_MODE')
        payload={n:owned(run,n,r) for n,r in m['files'].items()}
        release_manifest=json.loads((release/'release-opt.json').read_bytes())
        paths={1:'cpu/gelu.elf',2:'cpu/gelu.elf',3:'cpu-contract.json',4:'cpu-stages.json',5:'jobs.bin',6:'base/model.onnx',7:'base/matrix_descriptors.bin',8:'optimization-contract.json',9:'descriptors.bin',10:'layout.json',11:'program10/code.bin',12:'base/weights.bin'}
        auxiliary=dict(loading=load_breakdown(payload['load-timing.bin'],m['T_load_ns'],{a:release_manifest['files'][n]['bytes'] for a,n in paths.items()}),user_digest=digest_breakdown(payload['user-digest-audit.bin']),kernel_digest=kernel_digest_breakdown(payload['kernel-digest.csv'].decode('ascii'),m['generation'],catalog['layout']['execution_policy']['user_kernel_sha']=='auto'))
        runtime=json.loads(payload['cpu-runtime.json']);expected=json.loads((release/'cpu/runtime.json').read_bytes())
        need(all(runtime.get(k)==expected.get(k) for k in ('compiler','algorithm','threads','precision_flags','libm_bytes','libm_sha256')),'CPU_RUNTIME_IDENTITY')
        binding=payload['binding-opt.bin'];canonical=(release/'binding-opt.bin').read_bytes()
        need(len(binding)==512 and struct.unpack_from('<8I',binding)==(0x44424c48,4,512,4,10,31,m['generation'],1) and binding[32:]==canonical[32:] and sha(canonical)==verified['binding_sha256'],'BINDING_V4')
        gb=payload['gpu-records.bin'];wanted=[e for e in catalog['entries'] if e['kind']=='GPU'];prefix=m.get('mode')=='stage0'
        if prefix:wanted=wanted[:1]
        need(len(gb)==len(wanted)*448 and m['gpu_records']==len(wanted),'GPU_COVERAGE')
        previous=None;raw_cycles=0
        for i,e in enumerate(wanted):
            raw=gb[i*448:(i+1)*448];ev=raw[160:416];meta=[e[k] for k in ('first','last','first_stage','last_stage','op','block_id','owner_tiles')]+[len(e['materialized_stages'])]
            need(list(struct.unpack_from('<32I',raw))==e['words'] and list(struct.unpack_from('<8I',raw,128))==meta,'ARGS_CATALOG')
            need(struct.unpack_from('<4I',ev)==(0x454a4c48,1,256,0) and ev[16:32]==bytes.fromhex(m['run_id']),'GPU_HEADER_RUN')
            job,submit,done,ack=struct.unpack_from('<4Q',ev,32);status,errors,pw,pr,before,after,watchdog,elapsed=struct.unpack_from('<8I',ev,80)
            need(job>0 and submit<=done<=ack and elapsed>0 and watchdog==4000000000 and status&2 and not status&4 and errors==pw==pr==0 and after==(before+1)&0xffffffff,'GPU_COMPLETION')
            need(struct.unpack_from('<4I',ev,64)==(e['first'],e['op'],e['first_stage'],10),'GPU_ROLE')
            ready,idle,cdma,valid=struct.unpack_from('<4I',ev,112)
            need(ready==idle==1 and not cdma&0x4770 and valid&27==27 and not valid&~31 and bool(valid&4)==bool(e['owner_tiles']),'GPU_IDLE_GUARDS')
            need(struct.unpack_from('<4I',ev,128)==(0,0,0,0) and struct.unpack_from('<4I',ev,144)==(3,0,m['generation'],0) and ev[160:192].hex()==sha(raw[:128]) and struct.unpack_from('<Q',ev,192)[0]==submit+60000000000 and done<=submit+60000000000 and struct.unpack_from('<I',ev,208)[0]==3 and not any(ev[228:]),'GPU_CRT_EPOCH_DEADLINE')
            need(list(struct.unpack_from('<4I',ev,212))==expected_owner_counts(catalog,e),'OWNER_TILE_COVERAGE')
            if previous:need(job==previous[0]+1 and before==previous[1] and submit>=previous[2],'GPU_CHAIN')
            previous=(job,after,ack);raw_cycles+=elapsed
        if prefix:
            need(m.get('state')=='PREFIX_STOP' and m['logical_completed']==wanted[0]['last'] and m['cpu_records']==0,'PREFIX_ROLE')
            result.update(scope='HW4_OPT_STAGE0_RAW',execution='PREFIX_PASS',auxiliary=auxiliary,raw_gpu_cycles=raw_cycles,h0='W4_STAGE0_ONLY',h1='NOT_RUN');return result
        need(m.get('mode') in ('diagnostic','explore','measured') and m.get('logical_completed')==1023 and m.get('stage_count')==100 and m.get('cpu_records')==13,'FULL_RANGE')
        cb=payload['cpu-records.bin'];need(len(cb)==13*712,'CPU_COVERAGE')
        spec=importlib.util.spec_from_file_location('opt_frozen_cpu_receipt_auditor',ROOT/'scripts/hunet_fused/audit_run.py');old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
        schedule=json.loads((ROOT/'generated/hunet_fused/schedule.json').read_bytes());gi=ci=0;seen=set();cpu=[];diag=m['mode']=='diagnostic'
        counter=struct.unpack_from('<I',gb,160+96)[0]
        for entry in catalog['entries']:
            if entry['kind']=='GPU':counter=struct.unpack_from('<I',gb,gi*448+160+100)[0];gi+=1
            else:
                c=old.cpu_record(cb[ci*712:(ci+1)*712],schedule['cpu'][ci],bytes.fromhex(m['run_id']),m['generation'],counter,schedule['logical_sha256'],payload,diag)
                need(c['token'] not in seen,'CPU_TOKEN_REUSE');seen.add(c['token']);cpu.append(c);ci+=1
        need(ci==13 and gi==len(wanted),'LOGICAL_CPU_GPU_MAPPING')
        runtime=json.loads(payload['cpu-runtime.json']);expected=json.loads((release/'cpu/runtime.json').read_bytes())
        need(all(runtime.get(k)==expected.get(k) for k in ('compiler','algorithm','threads','precision_flags','libm_bytes','libm_sha256')),'CPU_RUNTIME_IDENTITY')
        begin=payload['begin.bin'];inp=payload['input.f32'];logits=payload['logits.f32'];end=payload['end.bin'];rid=bytes.fromhex(m['run_id'])
        input_record=json.loads((ROOT/'contracts/hunet_inputs.json').read_bytes())['records'][0]
        need(len(inp)==196608 and sha(inp)==input_record['sha256'],'INPUT_IDENTITY')
        need(len(begin)==128 and struct.unpack_from('<4I',begin)==(0x47424c48,1,128,0) and begin[16:32]==rid and struct.unpack_from('<4I',begin,32)==(0,m['sample_kind'],m['sample_index'],0) and struct.unpack_from('<Q',begin,56)[0]==196608 and begin[64:96].hex()==sha(inp) and not any(begin[96:]),'BEGIN_ROLE')
        need(len(end)==256 and struct.unpack_from('<4I',end)==(0x4e455948,1,256,0) and end[16:32]==rid and struct.unpack_from('<8I',end,32)==(0,m['generation'],1023,len(wanted),13,100,1,31),'END_COMPLETE')
        before,after=struct.unpack_from('<2I',end,72)
        need(struct.unpack_from('<Q',end,64)[0]==previous[0] and before==struct.unpack_from('<I',gb,256)[0] and after==previous[1] and ((after-before)&0xffffffff)==len(wanted) and end[80:112].hex()==sha(inp) and end[112:144].hex()==sha(logits) and end[144:176].hex()==verified['binding_sha256'] and not any(end[176:]),'END_HASH_COUNTER_CHAIN')
        need(len(logits)==32420,'LOGITS_SIZE')
        stage_names={'stages/%03d.f32'%s['stage'] for s in catalog['operator_coverage'] if s['materialized']}
        if diag:
            need(m['captured_stages']==len(stage_names) and {n for n in payload if n.startswith('stages/')}==stage_names,'MATERIALIZED_DIAGNOSTIC_COVERAGE')
            frozen=json.loads((ROOT/'generated/hunet/catalog.json').read_bytes())['stages']
            for s in frozen:
                name='stages/%03d.f32'%s['stage_id']
                if name in stage_names:need(len(payload[name])==s['bytes'],'STAGE_SHAPE');old.finite(payload[name])
            internal={}
            for entry in wanted:
                if catalog['layout']['execution_policy']['parallel_gn'] and entry['op'] in (4,16):
                    name='internal/%03u-gn-detail.bin'%entry['first'];internal[name]=gn_internal(payload[name],entry)
                if catalog['layout']['execution_policy']['internal_timing'] and entry['op']==15:
                    name='internal/%03u-fused-timing.bin'%entry['first'];raw=payload[name];need(len(raw)==4096 and raw[:64]==raw[4032:]==bytes([0xa5])*64,'FUSED_TIMING_PAGE_GUARDS');need(not any(raw[384:4032]),'FUSED_TIMING_PAGE_STATUS')
                    core=struct.unpack_from('<8Q',raw,64);warp=struct.unpack_from('<32Q',raw,128)
                    internal[name]=internal_cycles([list(core[i*2:(i+1)*2]) for i in range(4)],[[list(warp[c*8+w*2:c*8+w*2+2]) for w in range(4)] for c in range(4)])
            need({n for n in payload if n.startswith('internal/')}==set(internal),'INTERNAL_DIAGNOSTIC_COVERAGE');result['internal']=internal
        else:
            need(m['captured_stages']==0 and not any(n.startswith(('stages/','cpu/','internal/')) for n in payload),'DEFAULT_DIAGNOSTICS_OFF')
        capture=json.loads((run/'driver-capture.json').read_bytes())
        need(capture.get('run_id')==m['run_id'] and capture.get('generation')==m['generation'] and capture.get('boot_id')==m['boot_id'],'DRIVER_CAPTURE_EPOCH')
        for name in ('management.csv','submit.csv','kernel-digest.csv'):owned(run,name,capture['files'][name])
        need({p.relative_to(run).as_posix() for p in run.rglob('*') if p.is_file() or p.is_symlink()}==set(payload)|{'run.json','driver-capture.json','management.csv','submit.csv','kernel-digest.csv'},'EXACT_RUN_FILES')
        timing=summarize(run,catalog)
        # T22 verified release provides relocated, byte-frozen references.
        refs=verified['references']
        from scripts.hunet_opt.numeric import compare_outputs
        numeric=compare_outputs(payload['logits.f32'],{k:owned(release,r['path'],r) for k,r in refs.items() if k in ('r2','ort','labels')},owned(release,refs['history']['path'],refs['history']))
        need(numeric['runtime_finite'],'NONFINITE_LOGITS')
        result.update(scope='HW4_OPT_RAW_AUDITED',execution='PASS',auxiliary=auxiliary,timing=timing,numeric=numeric,raw_gpu_cycles=raw_cycles,h0='W4_OPT_SOFTWARE_RUN',h1='NOT_RUN')
        # Formal authority remains separate from numerical and protocol success.
        authority=gate.get('formal_numeric_and_human_authority',False)
        result['formal_eligible']=bool(numeric['formal_numeric_pass'] and m.get('mode')=='measured' and authority)
        result['publish_performance']=result['formal_eligible']
    except (Exception,) as error:result['reason']=str(error)
    return result
