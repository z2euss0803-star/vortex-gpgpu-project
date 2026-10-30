"""Existing integer packages through the native service; HOST PE, never board."""
import pathlib,subprocess,sys,json,time,hashlib,os
from scripts.unified.check_u11 import sha,artifact
from scripts.unified.capture_native_private import capture
from scripts.unified.build_catalog import build_catalog

def fixed_identity():
 paths=set()
 for base in ('core','kernel','driver','include/vxl','include/uapi','data/nnrt/programs/int16-family-v1','data/nnrt/packages/int16-mlp-a','tests/nnrt/fixtures/external-model-b','device/vxnn'):
  paths.update(p for p in pathlib.Path(base).rglob('*') if p.is_file())
 paths.update(pathlib.Path(p) for p in ('data/image/programs-v2/catalog.bin','data/p8/programs/catalog.bin','nnrt/ops/int16.cpp','include/vxnn/ops_int16.hpp'))
 assert len(paths)>30,'source/kernel/program and frozen references must be included'
 return {str(p):sha(p) for p in sorted(paths)}

def catalog_fixture(output):
 root=pathlib.Path('data/nnrt/programs/int16-family-v1');family=json.loads((root/'family.json').read_text());code=(root/'code.bin').read_bytes();schema=(root/'args-schema.bin').read_bytes();digest=lambda b:hashlib.sha256(b).digest();hw=family['hardware']
 assert len(code)==65536 and digest(code).hex()==family['code_sha256'] and digest(schema).hex()==family['schema_sha256']
 fields=[1,1,65537,0,0,len(code),len(code),0,0x80000000,0x80100040,128,0,0,0,1,0]
 digests=[digest(code),digest(b''),digest(schema),bytes.fromhex(hw['hardware_contract']),bytes.fromhex(hw['bit_sha256']),bytes.fromhex(hw['xsa_sha256']),bytes(32),digest(b'HOST int16 common service fixture, not image')]
 raw=build_catalog([dict(fields=fields,digests=digests,schema=schema)]);catalog=output/'catalog.bin';catalog.write_bytes(raw);entry=raw[64+64+6*32:64+64+7*32];programs=output/'programs';programs.mkdir();(programs/(entry.hex()+'.bin')).write_bytes(code)
 return catalog,programs,root/'family.json'

def main():
 name=sys.argv[1];red='--expect-red' in sys.argv[2:];mode=next((x for x in sys.argv[2:] if x!='--expect-red'),'normal');assert mode in ('normal','sanitize','thread');out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);record=dict(scope='HOST_NATIVE_INTEGER_EXTENSION_NOT_PHYSICAL_GPU',mode=mode,commands=[],gpu_raw_cycles=None);before=fixed_identity();record['frozen_sources_before']=before
 def run(args):
  args=list(map(str,args));log=out/('command-%02d.log'%len(record['commands']));start=time.monotonic()
  with log.open('xb') as f:completed=subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=None if args[0]=='make' else 240)
  text=log.read_text(errors='replace');record['commands'].append(dict(argv=args,natural_exit=completed.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));record.setdefault('private_evidence',[]).extend(capture(text,out,len(record['commands'])));print(text[-4000:],flush=True);assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','FATAL: ThreadSanitizer','runtime error:'));return completed.returncode,text
 try:
  jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  build=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
  if mode in ('sanitize','thread'):build+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  code,_=run(build+[out/'test_native_int16_extension','-j'+str(jobs)]);assert code==0,'compile failure is not behavioral RED'
  code,_=run(['python3','tests/nnrt/int16_package_fixture.py',out/'packages']);assert code==0
  catalog,programs,family=catalog_fixture(out)
  code,text=run([out/'test_native_int16_extension',out/'packages/A',out/'packages/B',catalog,programs,family,out/'output'])
  if red:
   assert code==1 and 'NATIVE_INTEGER_STARTUP_ERROR 95' in text and 'existing integer operators for model A and external model B' in text,'exact compiled behavior failure required'
   record['behavioral_red_observed']=True
  else:
   assert code==0 and 'U20_NATIVE_INTEGER_PACKAGE_ONLY_EXTENSION' in text
   code,text=run([out/'test_native_int16_extension',out/'packages/A',out/'packages/B',catalog,programs,family,out/'interleaved-output','interleave'])
   assert code==0 and 'U25_THREE_INSTALLED_NATIVE_REQUESTS_NINE_JOBS_EXACT_REFERENCE_COOKIE_INVERSION_PASS_HOST_NOT_GPU' in text
  record['frozen_sources_after']=fixed_identity();assert before==record['frozen_sources_after'],'driver/core/device program/golden references must not change'
  if not red:
   from scripts.unified.extension_audit import audit_extension
   f=json.loads(family.read_text());hw=f['hardware'];identity=dict(provider=1,contract=hw['hardware_contract'],bit=hw['bit_sha256'],xsa=hw['xsa_sha256'],compatible=hashlib.sha256(hw['compatible'].encode()).hexdigest());ops=['MatMulI16.v1','BiasI48.v1','ReLUI48.v1'];program_identity={'1:65537':f['code_sha256']}
   snapshot=dict(files=before,hardware=identity,operators=ops,programs=program_identity);b=pathlib.Path('tests/nnrt/fixtures/external-model-b');manifest=json.loads((b/'manifest.json').read_text());graph=json.loads((b/'graph.json').read_text());new_package=dict(id=manifest['model_id'],operators=[n['operator']['name'] for n in graph['nodes']],hardware=identity,programs=program_identity)
   record['extension_audit']=audit_extension(snapshot,dict(snapshot,files=record['frozen_sources_after']),[new_package],dict(before_files=before,after_files=record['frozen_sources_after'],new_models=[manifest['model_id']],execution=dict(state='HOST_NATIVE_C_OWNER_FROZEN_PE_REFERENCE_EXACT_NOT_GPU',gpu_raw_cycles=None)))
  record['natural_exit']=0
 except BaseException as error:record.update(natural_exit=1,error=repr(error));raise
 finally:
  record['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()
