"""Original image wire + Linux sole receiver software validation, never board."""
import json,os,pathlib,struct,subprocess,sys,time
from scripts.unified.check_u11 import artifact,sha

def main():
 name,mode=sys.argv[1:];assert mode in ('red','late-ack-red','normal','sanitize','thread','cross');jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='U15_LINUX_IMAGE_RECEIVER_HOST_PROTOCOL_NOT_GPU_DP',mode=mode,gpu_raw_cycles=None,commands=[],artifacts=[])
 def run(argv,expected=0):
  argv=[str(x) for x in argv];begin=time.monotonic();p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=p.stdout.decode('utf8','replace');print(text[-4000:],flush=True);assert p.returncode==expected,'natural exit '+str(p.returncode);assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'));return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];elf=out/'test_image_udp'
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'gcc','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux';cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++']
  run(cmd+[elf,'-j'+str(jobs)]);raw=elf.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==(183 if mode=='cross' else 62)
  if mode=='red':
   text=run(cmd+['test-unified-image-udp','-j1'],expected=2);assert 'U15_RX_RED frozen UDP receiver unavailable rc=-38' in text;rec['functional_red']='COMPILED_LINUX_RECEIVER_ENOSYS_CHILD83_NOT_MISSING_TOOL';print('U15_COMPILED_RECEIVER_RED_OBSERVED_NOT_ACCEPTANCE')
  elif mode=='late-ack-red':
   text=run([elf],expected=84);assert 'U15_LATE_ACK_RED op=7 state=4 length=64' in text;rec['functional_red']='COMPILED_ORIGINAL_PROTOCOL_LATE_ACK_APPLIED_WRONG_COMPLETION_OPCODE'
  elif mode=='cross':
   import re
   text=run([prefix+'readelf','-hW','-dW','-VW',elf]);needed=set(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions=set(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text));sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec.update(target_runtime=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions)),executed_target_elfs=False)
  else:
   text=run(cmd+['test-unified-image-udp','-j1']);assert 'U15_IMAGE_RX_ORIGINAL_WIRE_PINNED_INPUT_DP_GATED_PASS_HOST_NOT_GPU_DP' in text
  rec['natural_exit']=0;print('U15_'+mode.upper()+'_RECEIVER_CHECK_COMPLETE_HOST_NOT_BOARD')
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()
