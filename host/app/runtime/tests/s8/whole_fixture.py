"""Synthetic full evidence package. Never executable, deployable, or board PASS."""
from pathlib import Path
import csv,hashlib,io,json,struct,zlib
from scripts.s8.catalog import expected_cases,VECTOR_SHA
from scripts.s8.layout import initial_image,guard_byte
from scripts.s8.oracle import positive_inputs,matmul
def dump(path,obj):path.write_text(json.dumps(obj,sort_keys=True,indent=2)+'\n',encoding='utf-8')
def digest(data):return hashlib.sha256(data).hexdigest()
def table(row,file_id,post=False):
 header='#VXL_S8_TRACE_V1 file=%d jobs_capacity=256 operations_capacity=2048 jobs_used=1 operations_used=1 overflow=0 active=0 failed=0 abandoned=0 generation=2 job=1 sequence=1 revision=%d boot=%s\n'%(file_id,2 if post else 1,'09'+'00'*15)
 s=io.StringIO();writer=csv.DictWriter(s,fieldnames=list(row),lineterminator='\n');writer.writeheader();writer.writerow(row);return (header+s.getvalue()).encode()
def reseal(root):
 root=Path(root);pre=json.loads((root/'case-manifest.json').read_text()) if (root/'case-manifest.json').exists() else json.loads((root/'_seal.json').read_text())
 def rows(paths):return [dict(path=p.name,bytes=len(p.read_bytes()),sha256=digest(p.read_bytes())) for p in sorted(paths)]
 files=[p for p in root.iterdir() if p.is_file() and not p.name.startswith(('post-','_')) and p.name not in ('case-manifest.json','command.json')]
 pre['files']=rows(files);dump(root/'case-manifest.json',pre)
 post=dict(pre);post['files']=rows(files+[root/'case-manifest.json']+[root/n for n in ('post-binding.bin','post-jobs.csv','post-operations.csv')]);dump(root/'post-manifest.json',post)
def create(folder,case=1):
 folder=Path(folder);root=folder/'run';package=folder/'package';root.mkdir();package.mkdir()
 catalog=dict(version=1,seed=20261013,vector_sha256=VECTOR_SHA,cases=expected_cases());dump(root/'catalog.json',catalog);dump(root/'layout.json',dict(elf=dict(tls_base=0x80002000,tbss_stride=16)))
 digests=[bytes([i+1])*32 for i in range(14)];digests[0]=hashlib.sha256(b'EXPLICIT_SOFTWARE_TEST_ONLY').digest();digests[3]=hashlib.sha256((root/'layout.json').read_bytes()).digest();digests[5]=hashlib.sha256((root/'catalog.json').read_bytes()).digest()
 binding=struct.pack('<8I',0x31423853,1,512,5,11,1,1,0)+b''.join(digests)+bytes(32);(package/'binding.bin').write_bytes(binding)
 release=dict(version=1,scope='SOFTWARE_TEST_ONLY',profile=5,program=11,source_sha256=digests[0].hex(),binding_sha256=digest(binding));dump(package/'manifest.json',release);(root/'release-manifest.json').write_bytes((package/'manifest.json').read_bytes())
 for name in ('binding.bin','post-binding.bin'):(root/name).write_bytes(binding)
 item=next(r for r in expected_cases() if r['id']==case);k=item['k'];tag=hashlib.sha256(binding).digest()[:16];run_id=bytes([1])+bytes(15)
 words=[0x314a3853,1,128,case,0,k,1,0,0x81000040,0x81200040,0x20000,0x81400000,4096,0x81410000,0x81411000,65536,2,1]
 job=bytearray(struct.pack('<18I',*words)+run_id+tag+bytes(24));struct.pack_into('<I',job,104,zlib.crc32(job));(root/'job.bin').write_bytes(job)
 captures=[]
 for slot in range(16):
  data=bytearray(initial_image(5,slot))
  if slot==0 and case!=1:
   a,b=positive_inputs(case);values=matmul(a,b);raw=b''.join(v.to_bytes(6,'little',signed=True) for v in values);data[64:448]=raw;data[0x4c0:0x6c0]=struct.pack('<64q',*values)
   header=[0x31523853,1,256,case,0,2,1,0,0,0,k,0,19,24,6,0];data[0x3c0:0x400]=struct.pack('<16I',*header);data[0x400:0x410]=run_id;data[0x410:0x420]=tag;data[0x420:0x440]=struct.pack('<8I',1,64,k,0xffffff,0,1,0x454e4f44,0)
   rows=[(1,0,[0,0,k,1]),(2,1,[0,k,k,3]),(3,0,list(struct.unpack_from('<4I',raw))),(2,24,[0,k,k,7]),(4,0x11f,[0,k,k,7]),(2,31,[0,0,0,0])]
   for i,(event,index,lanes) in enumerate(rows):struct.pack_into('<8I',data,0x1c0+i*32,event,index,*lanes,15,0)
  captures.append((0x81400000+slot*4096,bytes(data)))
 identity=bytearray(initial_image(6,0))
 for h in range(64):struct.pack_into('<8I',identity,64+h*32,h//16,h%16//4,h%4,h,0xffff0000-h*8192,0x80002000+h*16,0x53540000^h,0x544c0000^h)
 for c in range(4):struct.pack_into('<8I',identity,0x840+c*32,0x31433853,1,c,2,1,0,16,0x454e4f44)
 struct.pack_into('<8I',identity,0x8c0,0x31473853,1,64,2,1,15,64,0x454e4f44);identity[0x8e0:0x8f0]=run_id;identity[0x8f0:0x900]=tag
 captures += [(0x81410000,bytes(identity)),(0x81411000,initial_image(7,0))]
 for index,gpu in enumerate((0x800f0000,0x800f0040,0x800fffc0)):captures.append((gpu,bytes(guard_byte(1,0,index,i) for i in range(64))))
 for h in range(64):captures.append((0xffff0000-(h+1)*8192,bytes(guard_byte(8,h,0,i) for i in range(64))))
 captures.append((0xffff0000,bytes(guard_byte(8,0,1,i) for i in range(64))))
 for which in range(2):
  for slot in range(16):
   base=(0x81200000 if which else 0x81000000)+slot*0x20000;size=item['b_bytes' if which else 'a_bytes'];region=4 if which else 3
   for index,off in enumerate((0,64+size,0x1ffc0)):captures.append((base+off,bytes(guard_byte(region,slot,index,i) for i in range(64))))
 assert len(captures)==182
 for index,(gpu,data) in enumerate(captures):(root/('capture-%03d-%08x.bin'%(index,gpu))).write_bytes(data)
 row=dict(job_id=1,generation=2,sequence=1,case_id=case,state=3,result=0,error_class=0,elapsed_raw=1234,elapsed_u64=1234,elapsed_address=0xa0000028,elapsed_valid=1,pre_ack=1,watchdog=4000000000,status=0x502,errors=0,pending_w=0,pending_r=0,jobs=1,jobs_before=0,cdma_status=0x1002,pre_ack_ready=1,pre_ack_idle=0,ack_ready=1,ack_idle=1,tex_pending=0,exit_valid=15,exit0=0,exit1=0,exit2=0,exit3=0,guard_valid=1,immutable_valid=1,input_valid=1,ack_valid=1,identity_valid=1,metadata_valid=1,metadata_count=0 if case==1 else 1,crt_guards=bytes(guard_byte(9,0,index,i) for index in range(3) for i in range(64)).hex(),run_id=run_id.hex(),binding_tag=tag.hex(),clock=1,valid_events=sum(1<<i for i in (0,3,4,5,6,8)),timing_invalid=0)
 for i,t in ((0,100),(3,200),(4,1000),(5,1050),(6,1100),(8,1400)):row.update({'event%d_first_ns'%i:t,'event%d_last_ns'%i:t,'event%d_calls'%i:1})
 for i in range(23):
  for key in ('ns','calls','bytes','first_ns','last_begin_ns','last_ns','valid','overflow'):row['metric%d_%s'%(i,key)]=0
 op=dict(job_id=1,generation=2,sequence=1,metric=0,begin_ns=100,end_ns=200,bytes=0,valid=1,result=0)
 (root/'jobs.csv').write_bytes(table(row,1));(root/'operations.csv').write_bytes(table(op,2));row['state']=1;(root/'post-jobs.csv').write_bytes(table(row,1,True));(root/'post-operations.csv').write_bytes(table(op,2,True))
 seal=dict(version=1,scope='SOFTWARE_TEST_ONLY',profile=5,program=11,case_id=case,generation=2,sequence=1,job_id=1,run_id=run_id.hex(),flags=0,binding_sha256=digest(binding),gpu_seconds=None,errors=[]);dump(root/'_seal.json',seal);reseal(root);dump(root/'command.json',dict(exit=0,origin='SOFTWARE_TEST_ONLY',source_sha256=release['source_sha256'],argv=['TEST_ONLY','NO_DEVICE']))
 return root,package/'manifest.json'
