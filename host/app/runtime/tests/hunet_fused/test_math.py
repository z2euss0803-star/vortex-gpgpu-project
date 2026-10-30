import pathlib,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class Math(unittest.TestCase):
 def test_pack_and_epilogue(self):
  temp=ROOT/'build/test-tmp';temp.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=temp) as directory:
   exe=pathlib.Path(directory)/'math'
   result=subprocess.run(['c++','-std=c++17','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror',
    '-I.','-Iinclude','-Ivendor/hunet/v5/include','tests/hunet_fused/test_math.cpp','-o',str(exe)],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
   self.assertEqual(result.returncode,0,result.stdout.decode())
   result=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
   self.assertEqual(result.returncode,0,result.stdout.decode())
   self.assertIn(b'HOST_INDEX_ROUNDING_PASS_NOT_GPU',result.stdout)
if __name__=='__main__':unittest.main()
