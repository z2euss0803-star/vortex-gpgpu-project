import unittest
from scripts.hunet_opt.guest_gate import guest_gate
class Gate(unittest.TestCase):
 def test_resource_and_toolchain_refusal(self):
  s=dict(host='z2euss-virtual-machine',uid=1000,mem_available=8*1024**3,free_bytes=30*1024**3,estimated_bytes=5*1024**3,margin_bytes=5*1024**3,heavy=[],sdk_target='aarch64-xilinx-linux',sdk_version='9.2.0',kernel_release='5.4.0-xilinx-v2020.1',measurement_ok=True)
  c=dict(known=True,c_heavy=False,scope='HUNET_OFFLINE_ONLY')
  self.assertEqual(guest_gate(s,c)['admission'],'PASS')
  for delta in (dict(mem_available=3*1024**3),dict(free_bytes=14*1024**3),dict(heavy=['bitbake']),dict(sdk_target='x86_64-linux'),dict(kernel_release='6.8'),dict(measurement_ok=False),dict(uid=0)):
   self.assertEqual(guest_gate(dict(s,**delta),c)['admission'],'REFUSED')
  for delta in (dict(known=False),dict(c_heavy=True)):
   self.assertEqual(guest_gate(s,dict(c,**delta))['admission'],'REFUSED')
if __name__=='__main__':unittest.main()
