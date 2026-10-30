import copy,importlib.util,json,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
class Receipt(unittest.TestCase):
 def test_no_marker_only_pass(self):
  path=ROOT/'validation/h0/receipt.py'
  self.assertTrue(path.exists(),'CNN RTL receipt verifier missing')
  spec=importlib.util.spec_from_file_location('cnn_rtl_receipt',path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'simulate.log').write_text('CNN_FULL_SOC_PASS\n')
   for value in ({'scope':'CPU_REFERENCE_NOT_GPU','pass':True},{'scope':'CNN_H0_FULL_SOC_RTL','pass':True,'exit':0,'log':'simulate.log'},{'scope':'CNN_H0_FULL_SOC_RTL','pass':True,'exit':0,'layers':[{'threads':63}]},{'scope':'CNN_H0_FULL_SOC_RTL','pass':True,'exit':1}):
    self.assertTrue(m.verify(p,value),'Unbound/partial/fake evidence must fail')
 def test_required_boundaries(self):
  text=(ROOT/'validation/h0/receipt.py').read_text()
  for gate in ('source_commit','inputs','waveform','programs','natural_exit','threads','guards','outputs','fence','launches'):
   self.assertIn(gate,text)
if __name__=='__main__':unittest.main()
