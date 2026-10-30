"""Synthetic protocol records, with actual frozen bare-metal reference bytes.
Never labels these generated records as new board execution evidence.
"""
import hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];VAULT=ROOT.parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def references():
 m=json.loads((ROOT/'contracts/hunet_v5.json').read_text(encoding='utf-8'));r={}
 for role in ['original_acceptance','original_R2','original_ORT_FP32']:
  f=next(x for x in m['files'] if x['role']==role);r[role]=dict(f,path=str(VAULT/f['path']))
 r['v5']={}
 for roi in [0,26]:
  f=next(x for x in m['files'] if x['role']=='logits_payload' and '/roi%d/'%roi in x['path']);r['v5'][str(roi)]=dict(f,path=str(VAULT/f['path']))
 return r
def fixture(base):
 run=base/'run';release=base/'release';run.mkdir();release.mkdir();ref=references()
 source='11'*32;rid='ab'*16;boot='11111111-2222-4333-8444-555555555555';generation=3
 profile=sha((ROOT/'contracts/profiles/hw4-hunet-v5.json').read_bytes());program=json.loads((ROOT/'generated/hunet/segments.json').read_text())[0]['wire_sha256'];m=json.loads((ROOT/'contracts/hunet_v5.json').read_text(encoding='utf-8'))
 hashes=[profile,program,source]+[next(x['sha256'] for x in m['files'] if x['role']==r) for r in ['weights','jobs_file','aux_file','onnx']]
 binding=struct.pack('<8I',0x44424c48,1,256,4,6,3,generation,1)+b''.join(bytes.fromhex(h) for h in hashes)
 (release/'release.json').write_text(json.dumps(dict(schema=1,source_commit='22'*20,source_digest=source,profile=4,program_sha256=program,binding_sha256=sha(binding[32:]),bit_sha256=m['profile']['bit_sha256'],xsa_sha256=m['profile']['xsa_sha256']))+'\n',encoding='utf-8')
 (release/'binding.bin').write_bytes(struct.pack('<8I',0x44424c48,1,256,4,6,3,0,0)+binding[32:]);(release/'jobs.bin').write_bytes((VAULT/next(x['path'] for x in m['files'] if x['role']=='jobs_file')).read_bytes());(release/'matrix_descriptors.bin').write_bytes((VAULT/next(x['path'] for x in m['files'] if x['role']=='aux_file')).read_bytes())
 inputf=VAULT/m['inputs']['records'][0]['path'];inputb=inputf.read_bytes();logits=Path(ref['v5']['0']['path']).read_bytes();jobs=(release/'jobs.bin').read_bytes();records=[]
 for i,j in enumerate(struct.iter_unpack('<32I',jobs)):
  b=bytearray(256);struct.pack_into('<4I',b,0,0x454a4c48,1,256,0);b[16:32]=bytes.fromhex(rid);t=(i+1)*10000000;struct.pack_into('<4Q',b,32,i+4,t,t+1000,t+1500);struct.pack_into('<4I',b,64,j[4],j[3],j[30],6);struct.pack_into('<8I',b,80,2,0,0,0,(i-1)&0xffffffff,i&0xffffffff,4000000000,100);struct.pack_into('<4I',b,112,1,1,0x1002,27);struct.pack_into('<4I',b,144,3,0,generation,0);b[160:192]=hashlib.sha256(struct.pack('<32I',*j)).digest();struct.pack_into('<2Q',b,192,t+60000000000,60000);struct.pack_into('<I',b,208,3);records.append(b)
 begin=bytearray(128);struct.pack_into('<4I',begin,0,0x47424c48,1,128,0);begin[16:32]=bytes.fromhex(rid);struct.pack_into('<4I',begin,32,0,3,0,0);struct.pack_into('<2Q',begin,48,0x1000,196608);begin[64:96]=hashlib.sha256(inputb).digest()
 end=bytearray(128);struct.pack_into('<4I',end,0,0x4e454c48,1,128,0);end[16:32]=bytes.fromhex(rid);struct.pack_into('<4I',end,32,0,1023,generation,31);struct.pack_into('<Q2I',end,48,1026,1022,1);end[64:96]=hashlib.sha256(logits).digest();end[96:128]=hashlib.sha256(inputb).digest()
 payloads={'binding.bin':binding,'begin.bin':begin,'end.bin':end,'job_args.bin':jobs,'job_evidence.bin':b''.join(records),'input.f32':inputb,'logits.f32':logits,'guards.bin':bytes([0xa5])*13568,'last_trace.u32':struct.pack('<1014I',*([1]*254+[2]*254+[3]*253+[4]*253))}
 for n,b in payloads.items():(run/n).write_bytes(b)
 meta=dict(scope='HUNET_LINUX_V1',transport='SCP_CAPTURE',boot_id=boot,build_id='HW4-LINUX-HUNET-V5-TEST',kernel='5.4.0-xilinx-v2020.1',profile=4,generation=generation,run_id=rid,roi_index=0,sample_kind=3,sample_index=0,mode='resident',T_load_ns=100,T_resident_ns=20000000000,T_submit_wait_ns=[2000]*1023,T_capture_ns=1000,T_diagnostic_capture_ns=0,T_transfer_ns=None,gpu_seconds=None)
 meta['files']={n:dict(bytes=len(b),sha256=sha(b)) for n,b in payloads.items()};(run/'run.json').write_text(json.dumps(meta)+'\n',encoding='utf-8')
 ref['gate']=dict(scope='TEST_PROTOCOL_NOT_BOARD',integrity='PASS',boot_id=boot,build_id=meta['build_id'],kernel=meta['kernel'],profile=4,minimum_generation=2,source_digest=source,bit_sha256=m['profile']['bit_sha256'],xsa_sha256=m['profile']['xsa_sha256'])
 return run,release,ref
def rehash(run,name):
 p=run/'run.json';m=json.loads(p.read_text());b=(run/name).read_bytes();m['files'][name]=dict(bytes=len(b),sha256=sha(b));p.write_text(json.dumps(m)+'\n')
