"""Independent evidence decoding; never infer board execution from build output."""
from pathlib import Path
import csv,hashlib,io,json,re,struct,zlib
from scripts.s8.catalog import expected_cases,load_catalog
from scripts.s8.layout import initial_image,verify_only_changes,guard_byte
from scripts.s8.oracle import positive_inputs,matmul,unpack48

def require(value,message):
    if not value:raise ValueError(message)
def integer(row,key,lo=0,hi=(1<<64)-1):
    value=row[key]
    if isinstance(value,str):
        if not value.isdigit():raise ValueError('invalid integer '+key)
        value=int(value)
    if type(value) is not int or not lo<=value<=hi:raise ValueError('invalid integer '+key)
    return value

def phase_deltas(values):
    if len(values)<2 or any(type(v) is not int or not 0<=v<2**44 for v in values):raise ValueError('44-bit raw phase values required')
    delta=[(b-a)%(2**44) for a,b in zip(values,values[1:])]
    if any(v>4000000000 for v in delta) or sum(delta)>4000000000:raise ValueError('unknown reset/discontinuous counter')
    return delta

def audit_timing(job,operations):
    raw=integer(job,'elapsed_raw',1,3999999999)
    if integer(job,'elapsed_u64')!=raw or integer(job,'elapsed_address')!=0xa0000028 or integer(job,'elapsed_valid')!=1 or integer(job,'pre_ack')!=1:raise ValueError('missing actual pre-ACK elapsed')
    if integer(job,'clock')!=1 or integer(job,'timing_invalid') or job.get('gpu_seconds') is not None:raise ValueError('unknown clock or unsupported seconds')
    times=[];mask=integer(job,'valid_events')
    for event in (0,3,4,8):
        if not mask&(1<<event) or integer(job,'event%d_calls'%event)<1:raise ValueError('missing envelope event')
        # FIRST_DONE is first observation; WAIT_RETURN is latest complete return.
        times.append(integer(job,'event%d_%s_ns'%(event,'last' if event==8 else 'first')))
    if times!=sorted(times):raise ValueError('kernel envelope inversion')
    nested=[]
    for row in operations:
        if integer(row,'clock')!=1:raise ValueError('cross-clock operation subtraction')
        a=integer(row,'begin_ns');b=integer(row,'end_ns')
        if a>b:raise ValueError('backwards operation')
        nested.append(dict(metric=integer(row,'metric'),begin_ns=a,end_ns=b,ns=b-a))
    exclusive=[b-a for a,b in zip(times,times[1:])]
    spans=sorted((max(r['begin_ns'],times[0]),min(r['end_ns'],times[-1])) for r in nested if r['end_ns']>times[0] and r['begin_ns']<times[-1])
    union=0;end=times[0]
    for a,b in spans:
        if b>end:union+=b-max(a,end);end=b
    return dict(valid=True,elapsed_raw=raw,gpu_seconds=None,kernel_clock='ktime_get_ns',exclusive_ns=exclusive,kernel_envelope_ns=times[-1]-times[0],nested_operations=nested,covered_union_ns=union,residual_ns=times[-1]-times[0]-union,residual_reason='Envelope minus clipped interval union; no causal attribution or sum of nested times')

def parse_job(data):
    require(len(data)==128,'job size');words=struct.unpack('<32I',data)
    require(words[:3]==(0x314a3853,1,128),'job format')
    names=('case_id','flags','k','core_mask','owner_warp','a_base','b_base','input_slot_stride','output_base','output_stride','identity_base','control_base','spin_limit','generation','sequence')
    j=dict(zip(names,words[3:18]));j.update(run_id=data[72:88],binding_tag=data[88:104])
    canonical=data[:104]+bytes(4)+data[108:]
    require(zlib.crc32(canonical)==words[26] and not any(data[108:]),'CRC/reserved')
    cases={r['id']:r for r in expected_cases()};require(j['case_id'] in cases,'unknown case');case=cases[j['case_id']]
    require(j['k']==case['k'] and (j['flags']==0 or j['flags']==1 and case['phase_allowed']),'case K/flags')
    require(j['core_mask'] in (1,2,4,8,15) and j['owner_warp']<4 and (j['case_id']!=120 or j['core_mask']==15),'core/warp')
    fixed=dict(a_base=0x81000040,b_base=0x81200040,input_slot_stride=0x20000,output_base=0x81400000,output_stride=4096,identity_base=0x81410000,control_base=0x81411000,spin_limit=65536)
    require(all(j[k]==v for k,v in fixed.items()) and j['generation']>0 and 0<j['sequence']<0xffffffff and any(j['run_id']) and any(j['binding_tag']),'fixed job identity')
    return j

def _metadata(data):
    names=('magic','version','bytes','case_id','sw_status','generation','sequence','core','warp','owner_warp','k','flags','valid_bits','pages_count','status_count','phase_count')
    m=dict(zip(names,struct.unpack_from('<16I',data)))
    m.update(run_id=data[64:80],binding_tag=data[80:96]);m.update(zip(('format','primary_elements','primary_accepted','primary_bitmap','firsterror_event','detail','done','recovery_elements'),struct.unpack_from('<8I',data,96)))
    require(not any(data[128:256]),'metadata reserved');return m

def expected_status(job):
    k=job['k'];cid=job['case_id'];family=cid>>8;variant=cid&255
    def row(op,index,lanes,mask=15,recovery=False):return ((variant<<16 if family>=201 else 0)|op|(0x8000 if recovery else 0),index,lanes,mask)
    if family<201:
        return [row(1,0,[0,0,k,1]),row(2,None,[0,k,k,3]),row(3,0,None),row(2,24,[0,k,k,7]),row(4,0x11f,[0,k,k,7]),row(2,31,[0,0,0,0])]
    recovery=[row(1,1,[0,0,1,1],recovery=True),row(2,None,[0,1,1,3],recovery=True),row(2,24,[0,1,1,7],recovery=True),row(4,0x11f,[0,1,1,7],recovery=True),row(2,31,[0,0,0,0],recovery=True)]
    code={201:2,202:4,203:5,204:7,205:6,206:3,207:1,208:8,209:9}[family]
    if family in (201,208,209):
        control=(4097 if variant else 0) if family==201 else (8194 if variant else 2)
        return [row(6 if family==209 else 1,control,[code,0,0,0],7 if family==209 else 15),row(2,31,[code,0,0,0])]+recovery
    result=[row(1,2,[0,0,2,1])];sticky=0
    if family==202:result += [row(3,0,[0,0,0,0]),row(2,31,[4,0,2,1])];sticky=4
    if family==204 and variant==0:result += [row(4,0x11f,[7,0,2,1])];sticky=7
    result += [row(2,None,[sticky,2,2,3])]
    if family==203:result += [row(3,24+variant,[0,0,0,0]),row(2,31,[5,2,2,3])];sticky=5
    if family==204 and variant==1:result += [row(4,0x11f,[7,2,2,3])];sticky=7
    if family==205:result += [row(5,31,[6,2,2,3])];sticky=6
    if family==206:result += [row(5,31,[3,0,0,0]),row(3,0,[0,0,0,0]),row(2,31,[3,0,0,0]),row(2,31,[0,2,2,3])]
    if family==207:result += [row(1,1,[1,0,0,0]),row(2,31,[1,0,0,0]),row(2,31,[0,2,2,3])]
    return result+[row(2,24,[sticky,2,2,7]),row(4,0x11f,[0,2,2,7]),row(2,31,[sticky,0,0,0])]+recovery

def audit_phase(data,core,warp):
    require(len(data)==512,'phase bytes');header=struct.unpack_from('<16I',data)
    require(header[:8]==(0x31503853,1,512,4095,core,warp,44,12) and not any(header[8:]) and not any(data[256:]),'phase header/reserved')
    raw=[]
    for i in range(12):
        event,flags,lo,hi=struct.unpack_from('<4I',data,64+i*16);require(event==i and flags==1,'phase event identity');raw.append((hi<<32)|lo)
    return dict(raw_cycles=raw,deltas=phase_deltas(raw),core=core,warp=warp,source='PER_CORE_BUSY_MCYCLE44',gpu_seconds=None)

def audit_slot(job,data,core,warp,template=None,inputs=None):
    require(len(data)==4096,'slot bytes');slot=core*4+warp;base=initial_image(5,slot) if template is None else template
    selected=bool(job['core_mask']&(1<<core)) and warp==job['owner_warp'];cid=job['case_id'];family=cid>>8;negative=201<=family<=209;compat=cid in (2,3,4,5)
    if not selected or cid==1:
        require(data==base,'unselected/identity slot modified');return dict(classification='UNCHANGED',exact=0)
    m=_metadata(data[0x3c0:0x4c0]);require((m['magic'],m['version'],m['bytes'],m['done'])==(0x31523853,1,256,0x454e4f44),'result header/DONE')
    for key in ('case_id','generation','sequence','owner_warp','k','flags','run_id','binding_tag'):require(m[key]==job[key],'metadata '+key)
    require(m['core']==core and m['warp']==warp and not m['sw_status'],'actual slot identity/software error')
    mutable=[(0x3c0,256)];primary=not negative or family not in (201,208,209);recovery_exact=0
    if compat:
        count={2:64,3:32,4:5,5:4}[cid];require(m['format']==(4 if cid==5 else cid) and m['valid_bits']==1 and m['primary_elements']==count,'compat format/count')
        require(not any(m[k] for k in ('primary_accepted','primary_bitmap','pages_count','status_count','phase_count','recovery_elements','firsterror_event','detail')),'invented compat S8 metadata')
        if cid==2:expected=struct.pack('<64i',*([63]*64))
        elif cid==3:expected=struct.pack('<32f',*[sum(((i+t)%3-1)*((t+j)%3-1) for t in range(8)) for i in range(8) for j in range(4)])
        else:expected=struct.pack('<%dI'%count,*[v*0x01010101 for v in ([0,4,8,12,6] if cid==4 else [0,4,8,12])])
        require(data[64:64+len(expected)]==expected,'NUMERIC_MISMATCH compat');mutable.append((64,len(expected)))
    else:
        require(m['format']==1,'raw48 format');valid=16|(1|2 if primary else 0)|(4|8 if negative else 0)|(32 if job['flags'] else 0)
        require(m['valid_bits']==valid and m['primary_elements']==(64 if primary else 0) and m['primary_accepted']==(job['k'] if primary else 0) and m['primary_bitmap']==(0xffffff if primary else 0) and m['pages_count']==(24 if primary else 0) and m['recovery_elements']==(64 if negative else 0),'primary/recovery counts/valid')
        if negative:
            require(inputs is not None and len(inputs)==2 and all(len(v)==32 for v in inputs),'negative actual input bytes required')
            a,b=tuple([list(struct.unpack_from('<8h',v,t*16)) for t in range(2)] for v in inputs)
        else:a,b=positive_inputs(cid,core,warp)
        def compare(k,raw_at,decoded_at):
            expected=matmul(a[:k],b[:k]);require(unpack48(data[raw_at:raw_at+384])==expected,'NUMERIC_MISMATCH signed48');require(list(struct.unpack_from('<64q',data,decoded_at))==expected,'NUMERIC_MISMATCH device decoded')
        if primary:compare(job['k'],64,0x4c0);mutable += [(64,384),(0x4c0,512)]
        if negative:compare(1,0x900,0xa80);recovery_exact=64;mutable += [(0x900,384),(0xa80,512)]
        records=expected_status(job);require(m['status_count']==len(records)<=16,'status count');mutable.append((0x1c0,512))
        for i,(event,index,lanes,mask) in enumerate(records):
            row=struct.unpack_from('<8I',data,0x1c0+i*32);require(row[0]==event and row[6:]==(mask,0),'status event/valid mask/reserved')
            require(1<=row[1]<=65536 if index is None else row[1]==index,'status index/poll count')
            expected=list(struct.unpack_from('<4I',data,64)) if lanes is None else lanes;require(list(row[2:6])==expected,'raw status lanes')
            if i==1 and not negative:require(m['detail']==row[1],'poll detail')
        require(not any(data[0x1c0+len(records)*32:0x3c0]),'unused status records')
        if negative:
            code={201:2,202:4,203:5,204:7,205:6,206:3,207:1,208:8,209:9}[family];cause={201:1,202:3,203:3,204:4,205:5,206:5,207:1,208:1,209:6}[family];require(m['detail']==code and m['firsterror_event']==((cid&255)<<16)|cause,'expected error classification')
        else:require(m['firsterror_event']==0,'unexpected first error')
        require(m['phase_count']==(12 if job['flags'] else 0),'phase count')
        if job['flags']:audit_phase(data[0x6c0:0x8c0],core,warp);mutable.append((0x6c0,512))
    require(verify_only_changes(base,data,mutable),'guard/gap/sentinel or forbidden region changed')
    return dict(classification='EXPECTED_PROTOCOL_ERROR' if negative else 'EXACT',exact=m['primary_elements'],recovery_exact=recovery_exact,phase_collected=bool(job['flags']))

def _read(root,name,limit=64*1024*1024):
    name=Path(name);require(not name.is_absolute() and '\\' not in str(name) and not any(p in ('.','..') for p in name.parts),'unsafe path')
    target=Path(root)
    for part in name.parts:
        target=target/part;require(not target.is_symlink(),'symlink evidence')
    require(target.is_file() and target.stat().st_size<=limit,'missing/oversized '+str(name));return target.read_bytes()

def _json(data):
    def unique(pairs):
        out={}
        for k,v in pairs:require(k not in out,'duplicate JSON key');out[k]=v
        return out
    return json.loads(data.decode('utf-8'),object_pairs_hook=unique)

def _digest(data):return hashlib.sha256(data).hexdigest()

def _sealed(root,name):
    doc=_json(_read(root,name,4*1024*1024));seen=set();require(doc['version']==1 and doc['profile']==5 and doc['program']==11 and doc['gpu_seconds'] is None and doc['errors']==[],'seal identity/errors')
    for key in ('version','profile','program','case_id','generation','sequence','job_id','flags'):integer(doc,key)
    for row in doc['files']:
        path=row['path'];require(path not in seen,'duplicate sealed path');seen.add(path);data=_read(root,path);require(len(data)==row['bytes'] and _digest(data)==row['sha256'],'file digest '+path)
    for name in ('job.bin','binding.bin','jobs.csv','operations.csv','catalog.json','layout.json','release-manifest.json'):require(name in seen,'missing sealed '+name)
    return doc,seen

def _table(root,name,file_id):
    lines=_read(root,name,4*1024*1024).decode('ascii').splitlines();require(len(lines)>=2 and lines[0].startswith('#VXL_S8_TRACE_V1 '),'trace version')
    header={}
    for part in lines[0].split()[1:]:
        key,value=part.split('=',1);require(key not in header,'duplicate trace metadata');header[key]=value
    for key in ('overflow','active','failed','abandoned'):require(header[key]=='0','trace '+key)
    require(header['file']==str(file_id) and header['jobs_capacity']=='256' and header['operations_capacity']=='2048' and re.fullmatch('[0-9a-f]{32}',header['boot']) and int(header['boot'],16)>0,'trace metadata')
    parsed=list(csv.reader(lines[1:]));keys=parsed[0];require(len(keys)==len(set(keys)) and None not in keys,'duplicate trace columns')
    require(all(len(r)==len(keys) for r in parsed[1:]),'truncated CSV row');rows=[dict(zip(keys,r)) for r in parsed[1:]]
    require(len(rows)==int(header['jobs_used' if file_id==1 else 'operations_used']),'trace row count')
    return header,rows

def _capture_plan(job):
    rows=[(0x81400000+i*4096,4096) for i in range(16)]+[(0x81410000,4096),(0x81411000,4096),(0x800f0000,64),(0x800f0040,64),(0x800fffc0,64)]
    rows += [(0xffff0000-(h+1)*8192,64) for h in range(64)]+[(0xffff0000,64)]
    case=next(r for r in expected_cases() if r['id']==job['case_id'])
    for which in range(2):
        for slot in range(16):
            base=(0x81200000 if which else 0x81000000)+slot*0x20000;payload=case['b_bytes' if which else 'a_bytes'];rows += [(base,64),(base+64+payload,64),(base+0x1ffc0,64)]
    require(len(rows)==182,'internal capture plan');return rows

def validate_image_acceptance(release,receipt,manifest_sha):
    require(release.get('hardware',{}).get('state')=='EXPORT_IDENTITY_VERIFIED_NOT_PHYSICAL_PASS','actual hardware export gate')
    require(receipt.get('scope')=='ACTUAL_S8_IMAGE_CONTENT_NOT_BOARD' and receipt.get('integrity')=='PASS' and type(receipt.get('exit')) is int and receipt['exit']==0,'actual image content receipt')
    for key,expected in (('source_sha256',release['source_sha256']),('binding_sha256',release['binding_sha256']),('release_manifest_sha256',manifest_sha),('bit_sha256',release['digests']['bit']),('xsa_sha256',release['digests']['xsa'])):require(receipt.get(key)==expected,'image/release '+key)
    for name in ('BOOT.BIN','boot.scr','image.ub'):
        row=receipt.get('files',{}).get(name,{})
        require(re.fullmatch('[0-9a-f]{64}',row.get('sha256','')) and type(row.get('bytes')) is int and row['bytes']>0,'image payload identity '+name)
    return True

def release_negative_inputs(root,release,cid,slot):
    selected=[r for r in release['cases'] if r['id']==cid];require(len(selected)==1 and len(selected[0]['objects'])==53,'negative release asset catalogue')
    result=[]
    for index,base in ((2+slot,0x81000000),(18+slot,0x81200000)):
        row=selected[0]['objects'][index];require(row['gpu']==base+slot*0x20000 and row['bytes']==0x20000,'negative asset slot')
        raw=_read(root,row['path'],0x20000);require(len(raw)==row['bytes'] and _digest(raw)==row['sha256'],'frozen actual input bytes')
        result.append(raw[64:96])
    return tuple(result)

def _audit(root,manifest,image_receipt=None):
    root=Path(root);manifest=Path(manifest);release=_json(_read(manifest.parent,manifest.name,4*1024*1024));scope=release['scope'];require(scope in ('S8_RELEASE','SOFTWARE_TEST_ONLY'),'release scope')
    pre,seen=_sealed(root,'case-manifest.json');post,final_seen=_sealed(root,'post-manifest.json');require('case-manifest.json' in final_seen,'post seal missing pre seal')
    for key in ('profile','program','case_id','generation','sequence','job_id','run_id','flags','binding_sha256','scope'):require(pre[key]==post[key],'pre/post '+key)
    test=scope=='SOFTWARE_TEST_ONLY';require(pre['scope']==('SOFTWARE_TEST_ONLY' if test else 'REAL_DRIVER_CAPTURE_PENDING_INDEPENDENT_AUDIT'),'synthetic evidence scope mismatch')
    command=_json(_read(root,'command.json'));require(type(command['exit']) is int and command['exit']==0 and command['source_sha256']==release['source_sha256'],'command exit/source')
    require(command['origin']==('SOFTWARE_TEST_ONLY' if test else 'TRUSTED_CONTROLLER_REAL_BOARD'),'trusted origin required')
    require(_read(root,'release-manifest.json')==_read(manifest.parent,manifest.name),'release manifest changed')
    binding=_read(root,'binding.bin',512);require(len(binding)==512 and binding==_read(manifest.parent,'binding.bin',512) and binding==_read(root,'post-binding.bin',512),'full binding bytes')
    require(struct.unpack_from('<8I',binding)==(0x31423853,1,512,5,11,1,1,0) and not any(binding[480:]),'binding header/reserved')
    digests=[binding[32+i*32:64+i*32] for i in range(14)];require(all(any(d) for d in digests),'missing digest')
    require(_digest(binding)==release['binding_sha256']==pre['binding_sha256'] and digests[0].hex()==release['source_sha256'],'binding/source identity')
    require(_digest(_read(root,'catalog.json'))==digests[5].hex() and _digest(_read(root,'layout.json'))==digests[3].hex(),'catalog/layout digest')
    catalog=load_catalog(Path(root)/'catalog.json');layout=_json(_read(root,'layout.json'))
    if not test:
        require(image_receipt is not None,'separate actual image audit receipt required')
        validate_image_acceptance(release,_json(Path(image_receipt).read_bytes()),_digest(_read(manifest.parent,manifest.name)))
    job=parse_job(_read(root,'job.bin',128));require(job['binding_tag']==hashlib.sha256(binding).digest()[:16],'job binding tag')
    for key in ('case_id','generation','sequence','flags'):require(pre[key]==job[key],'sealed job '+key)
    require(pre['run_id']==job['run_id'].hex(),'sealed run id')
    for name in ('error.json','session-error.json'):require(not (root/name).exists(),'retained error prevents success')
    captures={}
    for index,(gpu,n) in enumerate(_capture_plan(job)):
        name='capture-%03d-%08x.bin'%(index,gpu);require(name in seen,'missing capture');data=_read(root,name,n);require(len(data)==n,'capture bytes');captures[gpu]=data
    require(len([n for n in seen if n.startswith('capture-')])==182,'extra capture')
    slots=[audit_slot(job,captures[0x81400000+s*4096],s//4,s%4,inputs=release_negative_inputs(manifest.parent,release,job['case_id'],s) if job['case_id']>>8>=201 else None) for s in range(16)]
    identity=captures[0x81410000];base=initial_image(6,0);require(verify_only_changes(base,identity,[(64,2240)]),'identity guard/gap')
    for h in range(64):
        c,w,l,hart,sp,tp,stack_tag,tls_tag=struct.unpack_from('<8I',identity,64+h*32);top=0xffff0000-h*8192
        require((c,w,l,hart)==(h//16,h%16//4,h%4,h) and top-8128<=sp<=top and sp%16==0 and tp==layout['elf']['tls_base']+h*layout['elf']['tbss_stride'] and stack_tag==0x53540000^h and tls_tag==0x544c0000^h,'64 actual identities')
    for c in range(4):require(struct.unpack_from('<8I',identity,0x840+c*32)==(0x31433853,1,c,job['generation'],job['sequence'],0,16,0x454e4f44),'core completion')
    require(struct.unpack_from('<8I',identity,0x8c0)==(0x31473853,1,64,job['generation'],job['sequence'],15,64,0x454e4f44) and identity[0x8e0:0x8f0]==job['run_id'] and identity[0x8f0:0x900]==job['binding_tag'],'global completion')
    control=captures[0x81411000];require(verify_only_changes(initial_image(7,0),control,[(64,256)]),'control guard/gap');family=job['case_id']>>8
    for c in range(4):
        sync=struct.unpack_from('<16I',control,64+c*64);count=14 if family==206 else 13
        expected=(job['sequence'],0x20000|(count-5),0x20000|count,0) if family in (206,207) and job['core_mask']&(1<<c) else (0,0,0,0)
        require(sync[:4]==expected and not any(sync[4:]),'bounded owner handoff/reserved')
    for gpu,n in _capture_plan(job)[18:]:
        if gpu in (0x800f0000,0x800f0040,0x800fffc0):region,slot,index=1,0,(0 if gpu==0x800f0000 else 1 if gpu==0x800f0040 else 2)
        elif gpu>=0xfff70000:region,slot,index=8,((0xffff0000-gpu)//8192-1 if gpu<0xffff0000 else 0),(0 if gpu<0xffff0000 else 1)
        else:
            which=1 if gpu>=0x81200000 else 0;region=4 if which else 3;slot=(gpu-(0x81200000 if which else 0x81000000))//0x20000;off=(gpu-(0x81200000 if which else 0x81000000))%0x20000;index=0 if off==0 else 2 if off==0x1ffc0 else 1
        require(captures[gpu]==bytes(guard_byte(region,slot,index,i) for i in range(64)),'raw guard '+hex(gpu))
    header,jobs=_table(root,'jobs.csv',1);op_header,ops=_table(root,'operations.csv',2)
    for key in ('jobs_used','operations_used','generation','job','sequence','revision','boot'):require(header[key]==op_header[key],'CSV snapshot '+key)
    for key,value in (('generation',job['generation']),('job',pre['job_id']),('sequence',job['sequence'])):require(int(header[key])==value,'trace header job key')
    selected=[r for r in jobs if integer(r,'job_id')==pre['job_id'] and integer(r,'generation')==job['generation'] and integer(r,'sequence')==job['sequence']];require(len(selected)==1,'unique job row');row=selected[0]
    require(integer(row,'state')==3 and integer(row,'case_id')==job['case_id'] and row['run_id']==job['run_id'].hex() and row['binding_tag']==job['binding_tag'].hex(),'job snapshot key')
    for key in ('result','error_class','errors','pending_w','pending_r','tex_pending','timing_invalid','exit0','exit1','exit2','exit3'):require(integer(row,key)==0,'runtime failure '+key)
    for key in ('pre_ack_ready','ack_ready','ack_idle','guard_valid','immutable_valid','input_valid','ack_valid','identity_valid','metadata_valid'):require(integer(row,key)==1,'missing validated '+key)
    require(integer(row,'pre_ack_idle')==0 and (integer(row,'status')>>8&7)==5,'actual pre-ACK DONE state, not IDLE')
    require(integer(row,'status')&2 and not integer(row,'status')&4 and integer(row,'exit_valid')==15 and integer(row,'watchdog')==4000000000 and integer(row,'jobs')==(integer(row,'jobs_before')+1)&0xffffffff,'DONE/jobs increment/watchdog')
    cdma=integer(row,'cdma_status');require(cdma&2 and not cdma&0x4770,'raw CDMA status')
    require(integer(row,'metadata_count')==(0 if job['case_id']==1 else sum(bool(job['core_mask']&(1<<c)) for c in range(4))),'metadata count')
    require(bytes.fromhex(row['crt_guards'])==bytes(guard_byte(9,0,index,i) for index in range(3) for i in range(64)),'raw CRT guards')
    matching_ops=[]
    for r in ops:
        if integer(r,'job_id')==pre['job_id']:
            require(integer(r,'generation')==job['generation'] and integer(r,'sequence')==job['sequence'] and integer(r,'valid')==1 and integer(r,'result')==0 and integer(r,'metric')<23,'operation key/result')
            # v1 operations inherit their matched job's documented ktime domain.
            matching_ops.append(dict(r,clock=row['clock']))
    timing=audit_timing(row,matching_ops)
    require(integer(row,'valid_events')&(1<<5) and integer(row,'valid_events')&(1<<6),'missing ACK timestamps')
    require(integer(row,'event4_first_ns')<=integer(row,'event5_first_ns')<=integer(row,'event6_last_ns')<=integer(row,'event8_last_ns'),'ACK timing order')
    for metric in range(23):
        values={key:integer(row,'metric%d_%s'%(metric,key)) for key in ('ns','calls','bytes','first_ns','last_begin_ns','last_ns','valid','overflow')}
        require(values['valid'] in (0,1) and values['overflow']==0,'metric validity/overflow')
        require(not any(values.values()) if not values['valid'] else values['calls']>0 and values['first_ns']<=values['last_begin_ns']<=values['last_ns'],'metric missing/counter order')
    post_header,post_jobs=_table(root,'post-jobs.csv',1);post_ops_header,post_ops=_table(root,'post-operations.csv',2)
    require(post_header['boot']==header['boot'] and post_header['generation']==header['generation'] and int(post_header['revision'])>=int(header['revision']),'post-REAP snapshot identity')
    for key in ('revision','boot','generation','job','sequence'):require(post_header[key]==post_ops_header[key],'post files key')
    post_selected=[r for r in post_jobs if integer(r,'job_id')==pre['job_id']];require(len(post_selected)==1 and integer(post_selected[0],'state')==1,'healthy REAP state')
    for key in ('elapsed_raw','elapsed_u64','elapsed_address','jobs','jobs_before','cdma_status','run_id','binding_tag','crt_guards'):require(row[key]==post_selected[0][key],'post snapshot changed '+key)
    return dict(version=1,scope='SW_AUDITOR_ONLY' if test else 'REAL_BOARD_EVIDENCE_AUDIT',state='PASS',source_sha256=release['source_sha256'],binding_sha256=_digest(binding),case_id=job['case_id'],generation=job['generation'],sequence=job['sequence'],run_id=job['run_id'].hex(),slots=slots,timing=timing,gpu_raw_cycles=None if test else timing['elapsed_raw'],fixture_raw_cycles=timing['elapsed_raw'] if test else None,gpu_seconds=None,board_state='NOT_RUN' if test else 'EVIDENCE_CONSISTENT_SINGLE_CASE')

def audit(root,manifest,image_receipt=None):
    try:return _audit(Path(root),Path(manifest),image_receipt)
    except (OSError,KeyError,TypeError,IndexError,struct.error,UnicodeError) as e:raise ValueError('incomplete/invalid S8 evidence: '+str(e)) from e

def audit_failure(root,manifest):
    """Forensic inventory only: never convert missing or failed data to PASS."""
    root=Path(root);manifest=Path(manifest);release=_json(_read(manifest.parent,manifest.name));rows=[];missing=[]
    for name in ('jobs.csv','operations.csv','error.json','session-error.json','command.json'):
        if not (root/name).is_file():missing.append(name)
    if (root/'jobs.csv').is_file():
        lines=_read(root,'jobs.csv',4*1024*1024).decode('ascii').splitlines()
        require(len(lines)>=2 and lines[0].startswith('#VXL_S8_TRACE_V1 '),'failure trace format')
        rows=list(csv.DictReader(lines[1:]))
    classes={1:'HOST_REJECT',2:'EXPECTED_PROTOCOL_ERROR',3:'NUMERIC_MISMATCH',4:'UNEXPECTED_PROTOCOL',5:'RUNTIME_FAULT',6:'TIMING_INVALID',7:'IDENTITY_MISMATCH',8:'RESOURCE_REFUSED'}
    last=rows[-1] if rows else {};error_class=integer(last,'error_class') if 'error_class' in last else None
    files=[dict(path=p.name,sha256=_digest(p.read_bytes()),bytes=p.stat().st_size) for p in root.iterdir() if p.is_file() and not p.is_symlink()]
    return dict(version=1,state='FAIL_NOT_ACCEPTED',classification=classes.get(error_class,'UNCLASSIFIED_FAILURE'),scope='SW_AUDITOR_ONLY' if release['scope']=='SOFTWARE_TEST_ONLY' else 'FORENSIC_INVENTORY_NOT_ACCEPTANCE',source_sha256=release['source_sha256'],raw_snapshot=last,missing=missing,files=files,gpu_raw_cycles=None,gpu_seconds=None)

if __name__=='__main__':
    import argparse,sys
    p=argparse.ArgumentParser();p.add_argument('mode',choices=('audit','failure'));p.add_argument('--root',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--image-receipt',type=Path);a=p.parse_args()
    try:
        receipt=audit(a.root,a.manifest,a.image_receipt) if a.mode=='audit' else audit_failure(a.root,a.manifest)
        with a.out.open('x',encoding='utf-8') as f:json.dump(receipt,f,sort_keys=True,indent=2);f.write('\n')
        print(receipt['state']);sys.exit(0 if a.mode=='audit' else 1)
    except (ValueError,OSError) as e:print('AUDIT_REFUSED: '+str(e),file=sys.stderr);sys.exit(1)
