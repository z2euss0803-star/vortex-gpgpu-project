import io,json,pathlib,sys,time,unittest
from scripts.unified.texture_pillow import dependency

def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal','durability-red');out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='MOSAIC_TO_DUAL_LOD_HOST_FILES_NOT_GPU_OR_SERVICE_DP',gpu_raw_cycles=None,mode=mode)
 try:
  rec['dependency']=dependency();import importlib.util
  spec=importlib.util.spec_from_file_location('texture_package_tests','tests/unified/test_texture_package.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);suite=unittest.defaultTestLoader.loadTestsFromName('Package.test_payload_directories_durable_before_manifest_publication' if mode=='durability-red' else 'Package.test_build_mosaic_dual_lod_without_upgrading_evidence',module) if mode in ('red','durability-red') else unittest.defaultTestLoader.loadTestsFromModule(module);log=io.StringIO();begin=time.monotonic();result=unittest.TextTestRunner(stream=log,verbosity=2).run(suite);text=log.getvalue();(out/'tests.log').write_bytes(text.encode());print(text,flush=True);rec.update(tests=result.testsRun,failures=len(result.failures),errors=len(result.errors),elapsed_s=time.monotonic()-begin)
  if mode=='durability-red':assert len(result.failures)==1 and not result.errors and 'all payload directories durable before manifest publication' in text;rec['functional_red']='DIRECTORIES_NOT_SYNCED_BEFORE_MANIFEST'
  elif mode=='red':assert len(result.failures)==1 and not result.errors and 'Mosaic dual-LOD package must be built' in text;rec['functional_red']='FROZEN_DUAL_LOD_PACKAGE_MISSING'
  else:assert result.wasSuccessful() and result.testsRun==4 and not result.skipped
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_bytes((json.dumps(rec,indent=2)+'\n').encode())
if __name__=='__main__':main()
