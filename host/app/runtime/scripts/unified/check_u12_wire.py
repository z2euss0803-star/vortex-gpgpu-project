"""Actual controlled-wire validator on all frozen jobs, never GPU execution."""
import json,os,pathlib,re,struct,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('normal','sanitize','cross');out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='U12_CONTROLLED_WIRE_COMPONENT_NOT_FULL_GRAPH_OR_GPU',gpu_raw_cycles=None,commands=[],artifacts=[],mode=mode)
 def run(argv):
  argv=[str(x) for x in argv];begin=time.monotonic();p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=p.stdout.decode('utf8','replace');print(text[-5000:],flush=True);assert p.returncode==0,'natural exit '+str(p.returncode);assert not any(x in text for x in ('ERROR: AddressSanitizer','runtime error:','U12_WIRE_ASSERT'));return text
 try:
  run([sys.executable,'-m','tests.nnrt.test_hunet_rodata','build/U12-hunet-rv32-callback-audit-071'])
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];elfs=[out/'test_hunet_wire',out/'test_hunet_schema']
  if mode=='sanitize':cmd+=['SANITIZE=address,undefined']
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux';cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++']
  run(cmd+elfs+['-j1'])
  for elf in elfs:
   raw=elf.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==(183 if mode=='cross' else 62)
  if mode=='cross':
   needed=set();versions=set()
   for elf in elfs:
    text=run([prefix+'readelf','-hW','-dW','-VW',elf]);needed.update(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  else:
   text=run(cmd+['test-unified-hunet-wire','test-unified-hunet-schema','-j1']);assert 'U12_CONTROLLED_ARGS8_103_JOBS_314_DESCRIPTORS_BOUNDS_PASS_NOT_GPU' in text and 'U12_HUNET_EIGHT_REF_SEALED_SCHEMA_REAL_V2_RELOCATION_PASS_HOST_NOT_GPU' in text
  rec['natural_exit']=0;print('U12_'+mode.upper()+'_CONTROLLED_WIRE_COMPONENT_PASS_NOT_FULL_MIGRATION')
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
