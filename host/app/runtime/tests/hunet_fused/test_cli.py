import json,pathlib,subprocess,tempfile,unittest,os
ROOT=pathlib.Path(__file__).resolve().parents[2]
class Cli(unittest.TestCase):
 def test_no_compute_without_explicit_mode_and_gate(self):
  binary=ROOT/os.environ.get('FUSED_OUT','build')/'vxl_hunet_fused'
  help=subprocess.run([str(binary),'--help'],capture_output=True,text=True)
  self.assertEqual(help.returncode,0);self.assertIn('NO_DEFAULT_COMPUTE',help.stdout)
  with tempfile.TemporaryDirectory() as d:
   p=pathlib.Path(d)/'new';args=[str(binary),'--release','/not-a-release','--roi','0','--mode','measured','--out',str(p)]
   r=subprocess.run(args,capture_output=True,text=True);self.assertNotEqual(r.returncode,0)
   receipt=json.loads((p/'startup-failure.json').read_text());self.assertIn('NUMERIC_AND_AUTH_GATE',receipt['error'])
   before=(p/'startup-failure.json').read_bytes();r=subprocess.run(args,capture_output=True,text=True)
   self.assertNotEqual(r.returncode,0);self.assertEqual(before,(p/'startup-failure.json').read_bytes())
   p=pathlib.Path(d)/'kind-refused';args=[str(binary),'--release','/not-a-release','--roi','0','--mode','measured','--gate','/not-a-gate','--kind','diagnostic','--out',str(p)]
   r=subprocess.run(args,capture_output=True,text=True);self.assertNotEqual(r.returncode,0)
   self.assertIn('FORMAL_SAMPLE_KIND',json.loads((p/'startup-failure.json').read_text())['error'])
if __name__=='__main__':unittest.main()
