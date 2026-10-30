"""Image adapter metadata, input identity, typed ACK and display recovery fixtures only."""
import json,os,pathlib,struct,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('normal','sanitize','thread','cross');jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='U15_SHARED_OWNER_TRUSTED_RESULT_AND_TIME_METADATA_HOST_NOT_GPU_DP',mode=mode,gpu_raw_cycles=None,commands=[],artifacts=[],compiler_jobs=jobs,test_runner_jobs=1)
 def run(argv,expected=0):
  argv=[str(x) for x in argv];begin=time.monotonic();p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=p.stdout.decode('utf8','replace');print(text[-4000:],flush=True);assert p.returncode==expected,'natural exit '+str(p.returncode);assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:','U14_IMAGE_ASSERT'));return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];elf=out/'test_image_service'
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux'
   dep_path=pathlib.Path('build/U07-aarch64-012/receipt.json');dep=json.loads(dep_path.read_text());lib=next(x for x in dep['artifacts'] if x['path'].endswith('/json-install/lib/libjson-c.a'));assert dep['natural_exit']==0 and sha(lib['path'])==lib['sha256'];rec['json_dependency']=dict(receipt_sha256=sha(dep_path),library=lib);cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++','NN_JSON_PREFIX='+str(pathlib.Path(lib['path']).parent.parent)]
  elfs=[elf,out/'test_image_socket',out/'test_image_udp',out/'test_image_owner'];run(cmd+elfs+['-j'+str(jobs)])
  for target in elfs:
   raw=target.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==(183 if mode=='cross' else 62)
  for target in elfs[:2]:assert 'host_ps_read' not in run([prefix+'nm' if mode=='cross' else 'nm',target])
  if mode=='cross':
   needed=set();versions=set()
   for target in elfs:
    text=run([prefix+'readelf','-hW','-dW','-VW',target]);needed.update(__import__('re').findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions.update(__import__('re').findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text))
   sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  else:
   for case in ('flow','wrong-style','oom','acquire-oom'):
    text=run([elf,'data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin',case]);assert 'U15_TYPED_IMAGE_SERVICE_RESULT_SHA_DP_RETIREMENT_PASS_HOST_NOT_GPU_DP' in text
   text=run([out/'test_image_udp']);assert 'U15_IMAGE_RX_ORIGINAL_WIRE_PINNED_INPUT_DP_GATED_PASS_HOST_NOT_GPU_DP' in text
   for case in ('flow','guard','trace-error','final-trace-error','publish-error','binding','binding-zero','cancel','cancel-mid'):
    text=run([out/'test_image_owner','data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin',case]);assert 'PASS_HOST_MARKER_NOT_GPU' in text
   deps=pathlib.Path('/home/z2euss/workspace/unified_gpu_runtime_20261021/inputs/pc-sender-host-installed-052');proof=json.loads((deps/'receipt.json').read_text());assert proof['natural_exit']==0
   for item in proof['files']:assert sha(deps/item['path'])==item['sha256']
   rec['pc_host_dependency_receipt_sha256']=sha(deps/'receipt.json');previous=os.environ.get('PYTHONPATH');os.environ['PYTHONPATH']=str(deps/'site')+':'+(previous or '')
   if mode=='normal':os.environ['VX_IMAGE_RX_TEST_DELAY_MS']='12000'
   try:
    text=run([sys.executable,'tests/unified/test_image_sender.py',out/'test_image_socket',out/'pc-sender']);assert 'U15_ORIGINAL_UNMODIFIED_SENDER_WINDOW8_RETRANSMIT_STYLE_CACHE_PASS_HOST_NOT_GPU_DP' in text
   finally:
    os.environ.pop('VX_IMAGE_RX_TEST_DELAY_MS',None)
    if previous is None:os.environ.pop('PYTHONPATH',None)
    else:os.environ['PYTHONPATH']=previous
  rec['natural_exit']=0;print('U15_'+mode.upper()+'_MATRIX_PASS_HOST_PROTOCOL_NOT_GPU_OR_BOARD')
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
