"""Run unchanged P8 PC preprocessing tests with isolated matched host dependency."""
import hashlib,json,pathlib,shutil,subprocess,sys,time
from scripts.unified.texture_pillow import dependency

def main():
 name=sys.argv[1];out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='FROZEN_PC_PREPROCESS_ONLY_NOT_TEXTURE_OWNER_OR_GPU_DP',gpu_raw_cycles=None)
 try:
  rec['dependency']=dependency();private=out/'reference/src/p8';(private/'tests').mkdir(parents=True);(private/'desktop').mkdir();(out/'reference/reports/v23-warrior-adaptiveglaze-20261012').mkdir(parents=True)
  for n in ('desktop/texture_sender.py','tests/test_desktop_texture.py'):
   src=pathlib.Path('vendor/p8')/n;shutil.copyfile(str(src),str(private/n));assert src.read_bytes()==(private/n).read_bytes()
  # Run in this interpreter so the enum-namespace adapter applies while all
  # original source bytes and actual Pillow resampling code remain unchanged.
  import importlib.util,unittest,io
  spec=importlib.util.spec_from_file_location('frozen_preprocess_tests',str(private/'tests/test_desktop_texture.py'));module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);log=io.StringIO();begin=time.monotonic();result=unittest.TextTestRunner(stream=log,verbosity=2).run(unittest.defaultTestLoader.loadTestsFromModule(module));text=log.getvalue();(out/'tests.log').write_bytes(text.encode());print(text,flush=True);rec.update(tests=result.testsRun,failures=len(result.failures),errors=len(result.errors),skipped=len(result.skipped),elapsed_s=time.monotonic()-begin);assert result.wasSuccessful() and result.testsRun==11 and not result.skipped;rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_bytes((json.dumps(rec,indent=2)+'\n').encode())
if __name__=='__main__':main()
