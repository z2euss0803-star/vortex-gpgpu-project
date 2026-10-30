"""Focused final handoff gate, with raw failure timing and no GPU/board access."""
import hashlib,json,pathlib,re,shutil,struct,subprocess,sys,tarfile,time
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:3];assert mode in ('normal','sanitize','thread','cross','emulation')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(name=name,mode=mode,scope='U12_CPU_HANDOFF_HOST_PROTOCOL_NOT_GPU_DMA_OR_BOARD',gpu_raw_cycles=None,commands=[],artifacts=[])
 def run(argv,allowed=(0,)):
  start=time.monotonic();p=subprocess.run([str(x) for x in argv],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(rec['commands']));log.write_bytes(p.stdout)
  rec['commands'].append(dict(argv=[str(x) for x in argv],natural_exit=p.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=p.stdout.decode('utf8','replace');print(text[-4500:],flush=True)
  assert p.returncode in allowed,'natural exit '+str(p.returncode)
  assert not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:','U09_ASSERT'))
  return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];elf=out/'test_hunet_migration';execute=[elf]
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  if mode=='cross':
   env=json.loads(pathlib.Path('/home/z2euss/workspace/mosaic_hunet_dp_20261018/T20-image-0a0b2bb-20261020/actual-built-env.json').read_text());prefix=env['aarch64_prefix'];assert '9.2.0' in run([prefix+'g++','--version']) and run([prefix+'gcc','-dumpmachine']).strip()=='aarch64-xilinx-linux';cmd+=['CC='+prefix+'gcc','CXX='+prefix+'g++'];run(cmd+[elf,'-j1']);raw=elf.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==183
   text=run([prefix+'readelf','-hW','-dW','-VW',elf]);needed=set(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]',text));versions=set(re.findall(r'Name: ((?:GLIBC_|GLIBCXX_|CXXABI_|GCC_)[0-9.]+)',text));sys.path.insert(0,str(pathlib.Path('scripts/hunet').resolve()));from cross_check import runtime_inventory
   runtime=out/'runtime';runtime.mkdir();files,available=runtime_inventory(pathlib.Path(env['rootfs']),needed,runtime,prefix+'readelf');assert versions<=set(available);rec['target_runtime']=dict(rootfs_sha256=sha(env['rootfs']),files=files,required_versions=sorted(versions));rec['executed_target_elfs']=False
  else:
   if mode=='emulation':
    cross=pathlib.Path('build')/sys.argv[3];meta=json.loads((cross/'receipt.json').read_text());assert meta['natural_exit']==0 and meta['mode']=='cross'
    for row in meta['artifacts']:
     if row['kind']=='file':assert sha(row['path'])==row['sha256']
    tools=pathlib.Path('/home/z2euss/workspace/unified_gpu_runtime_20261021/tools/U08-qemu-user-static-211');package=next(tools.glob('qemu-user-static*.deb'));assert sha(package)=='6f9cb1236cfa22e34f25fb165b710629f6bd59172a1248da7138b74ede4e2a63'
    process=subprocess.Popen(['dpkg-deb','--fsys-tarfile',str(package)],stdout=subprocess.PIPE);frozen=None
    with tarfile.open(fileobj=process.stdout,mode='r|') as archive:
     for member in archive:
      if member.name.lstrip('./')=='usr/bin/qemu-aarch64-static':assert member.isfile();frozen=hashlib.sha256(archive.extractfile(member).read()).hexdigest()
    assert process.wait()==0 and frozen==sha(tools/'qemu-aarch64-static');qemu=tools/'qemu-aarch64-static';rec['qemu_sha256']=frozen;rec['qemu_version']=run([qemu,'--version']);cpu=run([qemu,'-cpu','help'],(1,));assert 'cortex-a53' in cpu and 'Available CPUs:' in cpu
    lib=out/'sysroot/lib';lib.mkdir(parents=True)
    for n,row in meta['target_runtime']['files'].items():
     src=cross/'runtime'/n;assert sha(src)==row['sha256'];shutil.copyfile(str(src),str(lib/n))
    elf=cross/'test_hunet_migration';execute=[qemu,'-cpu','cortex-a53','-L',out/'sysroot',elf];rec.update(scope='MATCHED_AARCH64_USER_EMULATED_HANDOFF_NOT_BOARD_NOT_PERFORMANCE',executed_target_elfs=True,cross_receipt_sha256=sha(cross/'receipt.json'),elf_sha256=sha(elf),rootfs_sha256=meta['target_runtime']['rootfs_sha256'])
   else:
    run(cmd+[elf,'-j1']);raw=elf.read_bytes();assert raw[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',raw,18)[0]==62
   inputs=json.loads(pathlib.Path('docs/unified/U08-gelu-inputs.json').read_text());source=pathlib.Path(inputs['target']);assert sha(source/'inputs-manifest.json')==inputs['manifest_sha256']
   for row in inputs['files']:assert sha(source/row['path'])==row['sha256']
   replay=out/'replay';replay.mkdir();text=run(execute+[source,replay]);rows=[json.loads(s) for s in text.splitlines() if s.startswith('{')];assert len(rows)==13 and sum(x['elements'] for x in rows)==1869056 and all(x['bit_diff_scalar']==0 and x['gpu_raw_cycles'] is None for x in rows)
   assert 'U12_HUNET_SHARED_POOL_COMMIT_DEPENDENCY_AND_FAILURE_PASS_HOST_PROTOCOL_NOT_GPU' in text
   if mode=='emulation':assert all(x['bit_diff_old_a53']==0 for x in rows)
   rec.update(replay=rows,full_staging_cleanup_timing=True,blocked_commit_dependency=True,failure_paths=10)
  for folder,dirs,files in os.walk(str(out)):
   for n in files:
    if n!='receipt.json':rec['artifacts'].append(artifact(pathlib.Path(folder)/n))
  rec['natural_exit']=0;print('U12_'+mode.upper()+'_FINAL_HANDOFF_COMPONENT_PASS_NOT_FULL_MIGRATION_GPU_CYCLES_NULL',flush=True)
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':
 import os
 main()
