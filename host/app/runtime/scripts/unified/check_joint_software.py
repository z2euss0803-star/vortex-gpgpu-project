"""One frozen Ubuntu software gate. CPU/C oracle and fake KMS are explicit;
no RV32 instruction, GPU inference, physical DP or board PASS is produced."""
import hashlib,json,pathlib,subprocess,sys,time,struct,os
from scripts.unified.cross_common import snapshot,sha
from scripts.unified.joint_firmware import generate
from scripts.unified.joint_conversion import convert,verify_conversion,wire
from scripts.unified.capture_native_private import capture

def main():
 mode=sys.argv[2] if len(sys.argv)>2 else 'normal';assert mode in ('normal','sanitize','thread')
 out=pathlib.Path('build')/sys.argv[1];out.mkdir(parents=True,exist_ok=False);before=snapshot('.');record=dict(scope='ACTUAL_UBUNTU_JOINT_SOFTWARE_HOST_C_ORACLE_FAKE_KMS_NOT_GPU_BOARD',mode=mode,source_before=before,commands=[],gpu_raw_cycles=None)
 build=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']
 if mode!='normal':build+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
 def run(argv,timeout=None):
  argv=list(map(str,argv));log=out/('command-%03d.log'%len(record['commands']));begin=time.monotonic()
  with log.open('xb') as f:p=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
  text=log.read_text(errors='replace');record['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),log_sha256=sha(log)));record.setdefault('private_evidence',[]).extend(capture(text,out,len(record['commands'])));(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n');print(text[-2500:],flush=True)
  if p.returncode or any(x in text for x in ('ERROR: AddressSanitizer','runtime error:','WARNING: ThreadSanitizer')):raise ValueError('actual gate command failed; raw retained: '+str(log))
  return text
 try:
  run([sys.executable,'-m','unittest','tests.unified.test_joint_image_binding','tests.unified.test_joint_controller','tests.unified.test_kernel_dt','tests.unified.test_s8_joint_platform','tests.unified.test_native_origin','tests.unified.test_image_payload','tests.unified.test_image_runtime_closure','tests.unified.test_image_boot','tests.unified.test_release_policy'])
  run(build+['test-unified-backend-cookie-order','-j1'])
  firmware=out/'joint-firmware';record['firmware']=generate(firmware)
  binaries=['vxgpud','test_native_p8_service','test_installed_p8','test_installed_image','test_installed_media','image_package_metadata','test_native_display','test_native_int16_extension']
  run(build+[out/x for x in binaries]+['test-unified-joint-bridge','test-unified-joint-daemon','-j1'])
  from scripts.unified.run_software import FOUNDATION
  controls=['test-v2-joint-catalog','test-unified-sequential-mode','test-unified-runtime-joint-mode','test-unified-joint-queued','test-unified-joint-cancel-drain','test-unified-joint-closed-reservation','test-unified-joint-cancel-queued','test-unified-joint-typed-reservation','test-unified-journal-joint','test-unified-compute-port','test-unified-deferred-port','test-unified-residency-switch','test-unified-journal-runtime','test-unified-control']
  run(build+FOUNDATION+controls+['-j1']);record['foundation_targets']=FOUNDATION;record['joint_and_control_targets']=controls
  # Reuse the exact already-compiled common owner/runtime objects in this mode,
  # not any previous validation result. The original integer PE model surrounds
  # real C object/pinning/ARGS/ACK code and compares complete frozen outputs.
  integer=out/'integer-cookie-regression';integer.mkdir()
  from scripts.unified.check_u20_integer_native import catalog_fixture
  run([sys.executable,'tests/nnrt/int16_package_fixture.py',integer/'packages']);catalog,programs,family=catalog_fixture(integer)
  args=[out/'test_native_int16_extension',integer/'packages/A',integer/'packages/B',catalog,programs,family,integer/'output']
  run(args);run(args+['interleave']);record['interleaved_integer_requests']=3;record['interleaved_integer_jobs']=9
  run([out/'test_installed_media']);run([out/'test_native_display','joint-cookie']);run([out/'test_native_display','flow'])
  for tag,catalog,programs,mode in [('old','data/p8/programs/catalog.bin','data/p8/programs','scene-service'),('joint',firmware/'catalog-v1.bin',firmware/'programs','joint-scene-service')]:
   run([out/'test_native_p8_service',catalog,programs,'unused',out/(tag+'-first-frame'),mode],600)
  run([out/'test_native_p8_service',firmware/'catalog-v1.bin',firmware/'programs','unused',out/'joint-descending-cookie','joint-cookie-order'],600)
  for tag,catalog,programs,extra in [('old','data/p8/programs/catalog.bin','data/p8/programs',[]),('joint',firmware/'catalog-v1.bin',firmware/'programs',['joint'])]:
   run([out/'test_installed_p8',catalog,programs,'data/p8',out/(tag+'-installed-p8')]+extra,600)
  run([out/'test_installed_image','data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin',out/'old-installed-image'],600)
  # Explicit synthetic FP32 HOST marker; this is a conversion/interface test,
  # never a newly executed Mosaic result or inherited historical GPU PASS.
  meta=json.loads(pathlib.Path('tests/unified/fixtures/native-mosaic-export-v1.json').read_bytes());origin=meta['native_origin'];origin.update(provider_flags=4,compatible='f670c653a37361f05f57c14eecd53dea1b87556489a2c1314efd208d715826dc',build_id='SYNTHETIC_JOINT_CONVERSION_HOST_ONLY');meta['build_id']=origin['build_id'];raw=b''.join(struct.pack('<f',x)*96 for x in (-1.,127.75,300.));tensor=hashlib.sha256(raw).hexdigest();meta['artifacts'][0]['sha256']=tensor;origin['tensor']=tensor
  source=out/'synthetic-original';source.mkdir(mode=0o700);(source/'output.f32').write_bytes(raw);(source/'mosaic-result.json').write_bytes(wire(meta))
  for p in source.iterdir():p.chmod(0o400)
  producer={k:origin[k] for k in ('boot_id','client_id','request_id')};receipt,package=convert(source/'mosaic-result.json',out/'actual-pinned-conversion',producer);expected=verify_conversion(receipt,package,producer,sha(source/'mosaic-result.json'),tensor);record['actual_converter_test']=dict(scope='SYNTHETIC_HOST_FP32_MARKER_NOT_MOSAIC_EXECUTION',package_id=expected,receipt_sha256=sha(out/'actual-pinned-conversion/conversion-receipt.json'),sealed_files=len(receipt['files']),conversion_ns=receipt['conversion_ns'])
  record.update(natural_exit=0,source_after=snapshot('.'),production_daemon=dict(path=str(out/'vxgpud'),bytes=(out/'vxgpud').stat().st_size,sha256=sha(out/'vxgpud')))
  if record['source_after']!=before:raise ValueError('frozen source changed during complete software gate')
 except BaseException as e:record.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()
