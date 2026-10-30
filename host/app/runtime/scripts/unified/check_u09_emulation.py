"""Execute matching SDK ELFs/libs privately; no A53 or GPU performance claim."""
import pathlib,json,hashlib,tarfile,subprocess,sys,shutil,time
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def main():
 name,cross_name=sys.argv[1:];out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);record=dict(scope='AARCH64_USER_EMULATION_NOT_BOARD_NOT_PERFORMANCE',gpu_raw_cycles=None,commands=[])
 def run(argv,allowed=(0,)):
  start=time.monotonic();p=subprocess.run([str(x) for x in argv],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(record['commands']));log.write_bytes(p.stdout);record['commands'].append(dict(argv=[str(x) for x in argv],natural_exit=p.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n');print(p.stdout.decode('utf8','replace')[-3000:],flush=True)
  if p.returncode not in allowed:raise RuntimeError(str(log)+' exit='+str(p.returncode))
  return p.stdout.decode('utf8','replace')
 try:
  tools=pathlib.Path('/home/z2euss/workspace/unified_gpu_runtime_20261021/tools/U08-qemu-user-static-211');package=next(tools.glob('qemu-user-static*.deb'));assert sha(package)=='6f9cb1236cfa22e34f25fb165b710629f6bd59172a1248da7138b74ede4e2a63'
  p=subprocess.Popen(['dpkg-deb','--fsys-tarfile',str(package)],stdout=subprocess.PIPE);frozen=None
  with tarfile.open(fileobj=p.stdout,mode='r|') as t:
   for m in t:
    if m.name.lstrip('./')=='usr/bin/qemu-aarch64-static':assert m.isfile();frozen=hashlib.sha256(t.extractfile(m).read()).hexdigest()
  assert p.wait()==0 and frozen==sha(tools/'qemu-aarch64-static');qemu=tools/'qemu-aarch64-static';record['qemu_sha256']=frozen;record['qemu_version']=run([qemu,'--version']);cpu=run([qemu,'-cpu','help'],(1,));assert 'cortex-a53' in cpu and 'Available CPUs:' in cpu
  cross=pathlib.Path('build')/cross_name;meta=json.loads((cross/'receipt.json').read_text());assert meta['natural_exit']==0
  for row in meta['artifacts']:assert pathlib.Path(row['path']).stat().st_size==row['bytes'] and sha(row['path'])==row['sha256']
  record['cross_receipt_sha256']=sha(cross/'receipt.json');record['rootfs_sha256']=meta['target_runtime']['rootfs_sha256'];lib=out/'sysroot/lib';lib.mkdir(parents=True);record['libraries']={}
  for name,row in meta['target_runtime']['files'].items():
   src=cross/'runtime'/name;assert sha(src)==row['sha256'];shutil.copyfile(str(src),str(lib/name));record['libraries'][name]=row['sha256']
  cmd=[qemu,'-cpu','cortex-a53','-L',out/'sysroot'];record['executed_elfs']={}
  for name in ['test_sched_scheduler','test_sched_cancel','test_sched_budget','test_sched_token_transfer','test_sched_scheduler_faults','test_sched_scheduler_alloc','test_sched_gelu_token','test_cpu_cpu']:
   elf=cross/name;record['executed_elfs'][name]=sha(elf);run(cmd+[elf])
  for mode in ['create','join','few']:run(cmd+[cross/'test_cpu_cpu',mode])
  inputs=json.loads(pathlib.Path('docs/unified/U08-gelu-inputs.json').read_text());source=pathlib.Path(inputs['target']);assert sha(source/'inputs-manifest.json')==inputs['manifest_sha256']
  for row in inputs['files']:assert sha(source/row['path'])==row['sha256']
  replay=out/'replay';replay.mkdir();elf=cross/'test_cpu_gelu';record['executed_elfs']['test_cpu_gelu']=sha(elf);text=run(cmd+[elf,source,replay]);rows=[json.loads(line) for line in text.splitlines() if line.startswith('{')];assert len(rows)==13 and sum(r['elements'] for r in rows)==1869056 and all(r['bit_diff_scalar']==0 and r['bit_diff_old_a53']==0 and r['gpu_raw_cycles'] is None for r in rows)
  record['replay']=rows;record['outputs']=[dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p)) for p in replay.iterdir()];record['natural_exit']=0;print('U09_MATCHED_AARCH64_EMULATED_SCHEDULER_AND_13_A53_REFERENCES_PASS_NOT_BOARD',flush=True)
 except Exception as e:record.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()
