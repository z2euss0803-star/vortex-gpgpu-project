"""Partial U12 gates plus impacted U11 regressions; no full graph/GPU claim."""
import json,os,pathlib,re,struct,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('normal','sanitize','cross');jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(name=name,mode=mode,scope='U12_MATH_TYPES_HANDOFF_COMPONENTS_NOT_FULL_MIGRATION',gpu_raw_cycles=None,commands=[],artifacts=[],executed_target_elfs=False,compiler_jobs=jobs,test_runner_jobs=1)
 def run(argv):
  start=time.monotonic();p=subprocess.run([str(x) for x in argv],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout)
  rec['commands'].append(dict(argv=[str(x) for x in argv],natural_exit=p.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=p.stdout.decode('utf8','replace');print(text[-4500:],flush=True)
  assert not p.returncode,'natural exit '+str(p.returncode)
  assert not any(x in text for x in ('ERROR: AddressSanitizer','runtime error:','U12_LAYOUT_ASSERT','U12_SCALAR_ASSERT','U12_FP16_ERROR','U11_MODEL_ASSERT','U11_INT16_ASSERT','U09_ASSERT','U07_ASSERT'))
  return text
 try:
  run([sys.executable,'-m','scripts.unified.generate_hunet_scalar','--check'])
  run([sys.executable,'-m','unittest','tests.nnrt.test_checkout_identity','-v'])
  family=json.loads(pathlib.Path('data/nnrt/programs/int16-family-v1/family.json').read_text())
  for row in family['source_rows']:assert sha(row['path'])==row['sha256'],'int16 family source drift'
  assert sha('data/nnrt/programs/int16-family-v1/code.bin')==family['code_sha256'] and sha('data/nnrt/programs/int16-family-v1/args-schema.bin')==family['schema_sha256'];rec['int16_family']=family
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];elfs=['test_hunet_layout','test_hunet_scalar','test_hunet_types','test_hunet_migration','test_int16_ops','test_int16_entry','test_int16_registry','test_int16_models','test_nn_package','test_nn_registry','test_nn_graph','test_nn_memory_plan']
  targets=['test-unified-hunet','test-unified-int16','test-unified-int16-entry','test-unified-int16-registry','test-unified-extension-plan','test-unified-package','test-unified-registry','test-unified-graph','test-unified-memory-plan','test-unified-package-legacy']
  if mode=='sanitize':cmd+=['SANITIZE=address,undefined']
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib)
   cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent)];run(cmd+[str(out/n) for n in elfs]+['-j'+str(jobs)]);needed=set();versions=set()
   for n in elfs:
    raw=(out/n).read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==183
    text=run([prefix+'readelf','-hW','-dW','-VW',out/n]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec['target_runtime']=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions))
  else:
   # Compile independent units/links concurrently only under the outer fresh
   # RAM/CPU/three-lock gate. Functional tests remain sequential.
   run(cmd+[str(out/n) for n in elfs]+['-j'+str(jobs)])
   text=run(cmd+targets+['-j1'])
   for marker in ('U12_PACK_35_STAGES_314_DESCRIPTORS_3266688_VALUES','U12_SCALAR_68_FROZEN_JOBS_PLUS_COPY_2814287_VALUES','U12_FP16_STORAGE_PLAN_PASS','U12_HUNET_SHARED_POOL_COMMIT_DEPENDENCY_AND_FAILURE_PASS'):assert marker in text
   inputs=json.loads(pathlib.Path('docs/unified/U08-gelu-inputs.json').read_text());source=pathlib.Path(inputs['target']);assert sha(source/'inputs-manifest.json')==inputs['manifest_sha256']
   for row in inputs['files']:assert sha(source/row['path'])==row['sha256']
   replay=out/'replay';replay.mkdir();text=run([out/'test_hunet_migration',source,replay]);rows=[json.loads(s) for s in text.splitlines() if s.startswith('{')];assert len(rows)==13 and sum(x['elements'] for x in rows)==1869056 and all(x['bit_diff_scalar']==0 and x['gpu_raw_cycles'] is None for x in rows);rec['replay']=rows
   rec.update(pack_stages=35,descriptors=314,packed_values=3266688,scalar_jobs=68,scalar_values_including_copy=2814287,new_graph_fp16_storage=True,new_model_values_exact=[64,128],semantic_negative_packages=13,scope_detail='Host protocol copy backend; no real DMA, GPU or whole HUNet execution')
   for n in elfs:
    raw=(out/n).read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==62
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  rec['natural_exit']=0;print('U12_'+mode.upper()+'_COMPONENTS_PLUS_U11_REGRESSION_PASS_NOT_FULL_MIGRATION_GPU_CYCLES_NULL',flush=True)
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
