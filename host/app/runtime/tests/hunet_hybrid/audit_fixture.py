"""Explicitly synthetic protocol bytes, never a model or hardware result."""
import hashlib,json,struct,sys,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/hunet_hybrid'))
import audit_run as auditor
def sha(b):return hashlib.sha256(b).hexdigest()
def put(path,data):path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
def js(path,v):put(path,json.dumps(v,sort_keys=True).encode())
def words(b,offset,*v):struct.pack_into('<%dI'%len(v),b,offset,*v)
def qw(b,offset,*v):struct.pack_into('<%dQ'%len(v),b,offset,*v)
def head(magic,n):b=bytearray(n);words(b,0,magic,1,n,0);return b
def fixture(root):
 run=root/'run';rel=root/'release';run.mkdir();rel.mkdir();rid=bytes.fromhex('1'*32);gen=2
 jobs=b''.join(struct.pack('<32I',*[int(x,16) for x in __import__('re').findall(r'0x([0-9a-fA-F]+)u',line)]) for line in (ROOT/'generated/hunet/catalog.h').read_text().splitlines() if line.startswith('{0x'))
 path=(ROOT/'release/CURRENT').read_text().strip()
 if os.name=='nt':path=path.replace('/mnt/f/','F:/')
 oldrel=Path(path)
 base=(oldrel/'binding.bin').read_bytes();role=auditor.freeze.freeze_contract(ROOT)
 js(rel/'cpu-stages.json',role);js(rel/'cpu-contract.json',auditor.pack.cpu_contract());put(rel/'cpu/gelu.elf',b'TEST_FIXTURE_CPU');put(rel/'base/jobs.bin',jobs);put(rel/'base/matrix_descriptors.bin',(oldrel/'matrix_descriptors.bin').read_bytes())
 binding=bytearray(struct.pack('<8I',0x44424c48,2,384,4,8,7,0,0)+base[32:]+hashlib.sha256(b'TEST_FIXTURE_CPU').digest()+hashlib.sha256((rel/'cpu-contract.json').read_bytes()).digest()+hashlib.sha256((rel/'cpu-stages.json').read_bytes()).digest()+bytes(32))
 put(rel/'binding-hybrid.bin',binding);js(rel/'release-hybrid.json',dict(scope='TEST_FIXTURE_HYBRID_NOT_CROSS',profile=4,program_id=8,source_digest=binding[96:128].hex(),binding_sha256=sha(binding)))
 gate=dict(integrity='PASS',scope='TEST_HYBRID_PROTOCOL_NOT_BOARD',boot_id='TEST_BOOT',build_id='TEST_BUILD',kernel='TEST_KERNEL',profile=4,minimum_generation=2,source_digest=binding[96:128].hex(),binding_sha256=sha(binding),cpu_compiler='TEST_COMPILER',libm_sha256='2'*64,expected_run_id=rid.hex())
 live=bytearray(binding);words(live,24,gen);put(run/'binding-hybrid.bin',live);js(run/'cpu-runtime.json',dict(compiler='TEST_COMPILER',algorithm='ERF_NATIVE_V1',threads=4,precision_flags=['-fno-fast-math','-ffp-contract=off'],libm_sha256='2'*64,libm_bytes=1))
 # Source identity comes from the frozen inputs contract, not invented zero images.
 data=(ROOT.parent/'hunet_original_tcu_20261008_v1/data/board_inputs/p1_001.f32').read_bytes();put(run/'input.f32',data)
 b=head(0x47424c48,128);b[16:32]=rid;words(b,32,0,4,0,0);qw(b,48,0x1000,len(data));b[64:96]=hashlib.sha256(data).digest();put(run/'begin.bin',b)
 put(run/'logical_args.bin',jobs);cpumap={s['sequence']:s for s in role['cpu_stages']};gpu=[];cpu=[];counter=0;gid=100;clock=1000000
 for seq,j in enumerate(struct.iter_unpack('<32I',jobs),1):
  put(run/('logical/%03u-args.bin'%seq),jobs[(seq-1)*128:seq*128])
  if seq not in cpumap:
   e=head(0x454a4c48,256);e[16:32]=rid;qw(e,32,gid,clock,clock+100,clock+200);words(e,64,seq,j[3],j[30],8);words(e,80,2,0,0,0,counter,counter+1,4000000000,100);words(e,112,1,1,0,27);words(e,144,3,0,gen,0);e[160:192]=hashlib.sha256(jobs[(seq-1)*128:seq*128]).digest();qw(e,192,clock+60000000000,1);words(e,208,3);put(run/('gpu/%03u-evidence.bin'%seq),e);gid+=1;counter+=1;clock+=1000
   row=dict(sequence=seq,T_submit_wait_ns=200,T_evidence_ns=100,T_reap_ns=100,T_capture_ns=100);gpu.append(row);instant=dict(role='GPU',**row)
  else:
   s=cpumap[seq];stage=s['stage'];n=s['elements']*4;pre=run/('cpu/%03u-'%stage);raw=bytes(n);h=hashlib.sha256(raw).digest();token=seq
   for suffix in ['input.f32','output.f32','writeback.f32','reference.f32']:put(Path(str(pre)+suffix),raw)
   b=head(0x42435948,128);b[16:32]=rid;words(b,32,gen,seq,stage,1);qw(b,48,token,0x1000,n,n);b[80:112]=h;put(Path(str(pre)+'begin.bin'),b)
   q=head(0x43435948,128);q[16:32]=rid;words(q,32,gen,seq,stage,1);qw(q,48,token,0x2000,n,10000);q[80:112]=h;words(q,112,4);put(Path(str(pre)+'commit.bin'),q)
   e=head(0x45435948,384);words(e,16,1,gen,seq,stage);e[32:48]=rid;qw(e,48,token);words(e,56,1,4);qw(e,64,n,n);e[80:112]=e[112:144]=h;e[144:176]=binding[160:192];qw(e,176,1000,1000,1000,100000,10000);words(e,216,counter,counter,0,2,0,63);qw(e,240,token^(gen<<32),60000000000);put(Path(str(pre)+'evidence.bin'),e);put(Path(str(pre)+'compute.bin'),struct.pack('<3Q2I',1000000,1010000,10000,4,0))
   row=dict(sequence=seq,stage=stage,T_begin_call_ns=1000,T_compute_ns=10000,T_commit_call_ns=1000,T_evidence_call_ns=100,T_writeback_capture_ns=1000,T_cpu_reference_ns=5000);cpu.append(row);instant=dict(role='CPU',**row)
  js(run/('metrics/%03u-timing.json'%seq),instant)
  if j[3]==2:
   active=min((j[7]+3)//4,4);values=[core+1 for core in range(active) for _ in range(j[7]//active+(core<j[7]%active))];put(run/('traces/%04u.u32'%seq),struct.pack('<%dI'%len(values),*values))
 for s in json.loads((ROOT/'generated/hunet/catalog.json').read_bytes())['stages']:put(run/('stages/%03u.f32'%s['stage_id']),bytes(s['bytes']))
 put(run/'logits.f32',bytes(32420));put(run/'guards.bin',b'\xa5'*13568);active=4;values=[core+1 for core in range(active) for _ in range(1014//active+(core<1014%active))];put(run/'last_trace.u32',struct.pack('<1014I',*values))
 e=head(0x4e455948,256);e[16:32]=rid;words(e,32,0,gen,1023,1010,13,100,1,31);qw(e,64,gid-1);words(e,72,0,counter);e[80:112]=hashlib.sha256(data).digest();e[112:144]=hashlib.sha256(bytes(32420)).digest();e[144:176]=hashlib.sha256(binding).digest();put(run/'end.bin',e)
 js(run/'timing.json',dict(gpu=gpu,cpu=cpu));meta=dict(scope='HUNET_HYBRID_LINUX_V1',transport='SCP_CAPTURE',state='RAW_COMPLETE_REQUIRES_AUDIT',error=0,boot_id='TEST_BOOT',build_id='TEST_BUILD',kernel='TEST_KERNEL',profile=4,program_id=8,generation=gen,run_id=rid.hex(),roi_index=0,sample_kind=4,sample_index=0,mode='diagnostic',logical_completed=1023,gpu_records=1010,cpu_records=13,T_load_ns=1000000,T_resident_ns=1000000000,T_checkpoint_ns=1000000,T_diagnostic_capture_ns=101000,T_transfer_ns=None,gpu_seconds=None)
 meta['files']={p.relative_to(run).as_posix():dict(bytes=p.stat().st_size,sha256=sha(p.read_bytes())) for p in run.rglob('*') if p.is_file()};js(run/'run.json',meta)
 return run,rel,dict(gate=gate)
