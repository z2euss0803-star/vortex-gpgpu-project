"""Native S8 NN+IMAGE common C owner/queue coverage, never GPU mathematics."""
import pathlib,subprocess,sys,json,time,struct,hashlib,os
from scripts.unified.mosaic_program import selected_directory
from scripts.unified.check_u11 import sha,artifact
from scripts.unified.capture_native_private import capture
from scripts.unified.build_catalog import build_catalog

def entries(path):
 raw=pathlib.Path(path).read_bytes();magic,version,size,count=struct.unpack_from('<4I',raw)
 assert magic==0x434c5856 and version==1 and size==len(raw) and 1<=count<=16 and hashlib.sha256(raw[64:]).digest()==raw[16:48]
 result=[]
 for i in range(count):
  offset=64+i*512;fields=list(struct.unpack_from('<16I',raw,offset));digests=[raw[offset+64+k*32:offset+96+k*32] for k in range(8)];schema=raw[fields[11]:fields[11]+fields[12]]
  assert len(schema)==fields[12] and hashlib.sha256(schema).digest()==digests[2]
  result.append(dict(fields=fields,digests=digests,schema=schema))
 return result

def union_fixture(mosaic,output):
 image=pathlib.Path('data/image/programs-v2');images=entries(image/'catalog.bin');neural=entries(mosaic);assert len(images)==10 and len(neural)==1
 rows=images+neural;identity=images[0]['digests'][3:6]
 assert all(row['digests'][3:6]==identity and row['fields'][14]==1 for row in rows),'actual provider identities must match; no synthetic bit rewrite'
 assert all(row['fields'][0]==3 for row in images) and neural[0]['fields'][:3]==[1,1,65568]
 payloads=[]
 for row in rows:
  f=row['fields'];length=max(f[4]+f[5],f[6]+f[7]);assert length<=8388608
  if f[0]==3:payload=(image/(row['digests'][6].hex()+'.bin')).read_bytes()
  else:
   payload=bytearray(length);code=(selected_directory('.')/'code.bin').read_bytes();ro=(selected_directory('.')/'rodata.bin').read_bytes();assert len(code)==f[5] and len(ro)==f[7];payload[f[4]:f[4]+f[5]]=code;payload[f[6]:f[6]+f[7]]=ro;payload=bytes(payload)
  assert len(payload)==length and hashlib.sha256(payload[f[4]:f[4]+f[5]]).digest()==row['digests'][0]
  if f[7]:assert hashlib.sha256(payload[f[6]:f[6]+f[7]]).digest()==row['digests'][1]
  payloads.append(payload)
 output.mkdir();programs=output/'programs';programs.mkdir();binary=build_catalog(rows);catalog=output/'catalog.bin';catalog.write_bytes(binary)
 for i,payload in enumerate(payloads):
  old=rows[i];new=entries(catalog)[i];assert new['fields'][:11]==old['fields'][:11] and new['fields'][14:]==old['fields'][14:] and new['digests'][:6]==old['digests'][:6] and new['digests'][7]==old['digests'][7]
  target=programs/(new['digests'][6].hex()+'.bin');target.write_bytes(payload);target.chmod(0o400)
 return dict(scope='HOST_SAME_PROVIDER_UNION_CATALOG_FIXTURE_NOT_IMAGE',image_catalog=sha(image/'catalog.bin'),nn_catalog=sha(mosaic),catalog_sha256=sha(catalog),entries=len(rows),gpu_raw_cycles=None)

def main():
 name=sys.argv[1];mode=sys.argv[2] if len(sys.argv)>2 else 'normal';assert mode in ('normal','sanitize','thread');out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);record=dict(scope='S8_NATIVE_NN_IMAGE_SAME_C_OWNER_HOST_MARKER_NOT_GPU_NUMERIC_DP',mode=mode,commands=[],gpu_raw_cycles=None)
 def run(args):
  args=list(map(str,args));log=out/('command-%02d.log'%len(record['commands']));begin=time.monotonic()
  with log.open('xb') as f:completed=subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=None if args[0]=='make' else 240)
  text=log.read_text(errors='replace');record['commands'].append(dict(argv=args,natural_exit=completed.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));record.setdefault('private_evidence',[]).extend(capture(text,out,len(record['commands'])));print(text[-3000:],flush=True);assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','FATAL: ThreadSanitizer','runtime error:'));return completed.returncode,text
 try:
  env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text())
  from scripts.unified.generate_mosaic_package import generate
  weights=pathlib.Path(env['mosaic_assets']);small=out/'input-small.f32';small.write_bytes(b''.join(struct.pack('<f',(i%37-18)*.025) for i in range(288)));model=out/'model-small';record['mosaic_package']=generate(weights,weights.parent/'manifest.json',small,12,8,64,model)
  for p in model.iterdir():p.chmod(0o400)
  model.chmod(0o500)
  jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  build=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):build+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  code,_=run(build+[out/'test_end_to_end',out/'mosaic-owner-catalog.bin','-j'+str(jobs)]);assert code==0
  record['union_fixture']=union_fixture(out/'mosaic-owner-catalog.bin',out/'union')
  code,text=run([out/'test_end_to_end',out/'union/catalog.bin',out/'union/programs','vendor/image-studio/gpu_programs/tables-64KiB.bin',out/'output',model]);assert code==0 and 'U20_S8_NATIVE_NN_IMAGE_SAME_C_OWNER_SHARED_QUEUE_AND_ORIGINAL_RETIREMENT_PASS_HOST_MARKER_NOT_GPU_DP' in text
  record['natural_exit']=0
 except BaseException as error:record.update(natural_exit=1,error=repr(error));raise
 finally:
  record['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()
