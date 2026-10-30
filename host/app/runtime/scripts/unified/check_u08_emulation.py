"""Matching target ELF/libm under a private user emulator, never board evidence."""
import hashlib,json,pathlib,shutil,subprocess,sys,tarfile,time
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def main():
 name=sys.argv[1];out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 record=dict(scope='AARCH64_CORTEX_A53_USER_EMULATION_NOT_BOARD_NOT_PERFORMANCE',gpu_raw_cycles=None,commands=[])
 def run(argv,allowed=(0,)):
  start=time.monotonic();p=subprocess.run([str(x) for x in argv],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%03d.log'%len(record['commands']));log.write_bytes(p.stdout);record['commands'].append(dict(argv=[str(x) for x in argv],natural_exit=p.returncode,elapsed_s=time.monotonic()-start,log=str(log),sha256=sha(log)));(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n');print(p.stdout.decode('utf8','replace')[-4000:],flush=True)
  if p.returncode not in allowed:raise RuntimeError(str(log)+' exit='+str(p.returncode))
  return p.stdout.decode('utf8','replace')
 try:
  tools=pathlib.Path('/home/z2euss/workspace/unified_gpu_runtime_20261021/tools/U08-qemu-user-static-211');package=next(tools.glob('qemu-user-static*.deb'));assert sha(package)=='6f9cb1236cfa22e34f25fb165b710629f6bd59172a1248da7138b74ede4e2a63'
  qemu=tools/'qemu-aarch64-static';p=subprocess.Popen(['dpkg-deb','--fsys-tarfile',str(package)],stdout=subprocess.PIPE);frozen=None
  with tarfile.open(fileobj=p.stdout,mode='r|') as t:
   for m in t:
    if m.name.lstrip('./')=='usr/bin/qemu-aarch64-static':assert m.isfile();frozen=hashlib.sha256(t.extractfile(m).read()).hexdigest()
  assert p.wait()==0 and frozen and sha(qemu)==frozen;record['qemu_sha256']=frozen;record['qemu_version']=run([qemu,'--version']);cpus=run([qemu,'-cpu','help'],allowed=(1,));assert 'Available CPUs:' in cpus and 'cortex-a53' in cpus;record['cpu_help_expected_natural_exit']=1
  cross=pathlib.Path('build/U08-aarch64-010');meta=json.loads((cross/'receipt.json').read_text());assert meta['natural_exit']==0
  for row in meta['artifacts']:assert pathlib.Path(row['path']).stat().st_size==row['bytes'] and sha(row['path'])==row['sha256']
  prefix=out/'sysroot';lib=prefix/'lib';lib.mkdir(parents=True);record['runtime']={}
  for key,row in meta['target_runtime']['files'].items():
   p=cross/'runtime'/key;assert sha(p)==row['sha256'];shutil.copyfile(str(p),str(lib/key));record['runtime'][key]=row['sha256']
  record['rootfs_sha256']=meta['target_runtime']['rootfs_sha256'];cmd=[qemu,'-cpu','cortex-a53','-L',prefix]
  for mode in (None,'create','join','few'):run(cmd+[cross/'objects/test_cpu_cpu']+([mode] if mode else []))
  inputs=json.loads(pathlib.Path('docs/unified/U08-gelu-inputs.json').read_text());source=pathlib.Path(inputs['target']);assert sha(source/'inputs-manifest.json')==inputs['manifest_sha256']
  for row in inputs['files']:assert sha(source/row['path'])==row['sha256']
  outputs=out/'replay';outputs.mkdir();text=run(cmd+[cross/'objects/test_cpu_gelu',source,outputs]);rows=[json.loads(s) for s in text.splitlines() if s.startswith('{')];assert len(rows)==13 and all(r['gpu_raw_cycles'] is None and r['bit_diff_scalar']==0 for r in rows)
  record['replay']=dict(scope='MATCHED_AARCH64_EMULATED_MATH_NOT_BOARD',rows=rows,old_a53_bit_diffs=sum(r['bit_diff_old_a53'] for r in rows),gpu_reference_bit_diffs=sum(r['bit_diff_gpu_reference'] for r in rows))
  record['outputs']=[dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p)) for p in outputs.iterdir()];(out/'numeric-comparison.json').write_text(json.dumps(record['replay'],indent=2)+'\n')
  assert record['replay']['old_a53_bit_diffs']==0,'frozen A53 reference mismatch retained'
  record['natural_exit']=0;print('U08_AARCH64_EMULATED_13_FROZEN_A53_BITWISE_PASS_NOT_BOARD',flush=True)
 except Exception as e:record.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
 return 0
if __name__=='__main__':sys.exit(main())
