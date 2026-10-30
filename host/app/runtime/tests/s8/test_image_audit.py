import copy,importlib,unittest
class Audit(unittest.TestCase):
 def test_wrong_dt_old_rootfs_missing_binding_and_payload_reject(self):
  api=importlib.import_module('scripts.s8.image')
  dt=dict(compatible='vortex,hw4-s8-validation-v1',resources=[0xa0000000,65536,0xb0010000,65536],pool=[0x10000000,0x100000],pool_type='shared-dma-pool',no_map=True,placement=True,pool_link=True,ready_link=True,clock_id=71,reset_id=116,gpio_clock_id=71,cdma_owners=0,old_gpu_disabled=True,sd_variant='sd-compat-two-flags-trial-a1',sd_no_1_8_v=True,sd_disable_wp=True,memory=[[0,0x80000000]],bootargs='earlycon console=ttyPS0,115200 root=/dev/ram0 rw')
  self.assertEqual(api.dt_errors(dt),[])
  for key,val in [('compatible','vortex,hw4-hunet-v5'),('pool',[0x10000000,0x10000]),('resources',[0xb0010000,65536,0xa0000000,65536]),('cdma_owners',1),('memory',[[0x500000000,0x80000000]])]:
   changed=dict(dt);changed[key]=val;self.assertTrue(api.dt_errors(changed),key)
  self.assertEqual(api.content_errors({'binding':'a','program':'b'},{'binding':'a','program':'b'}),[])
  for key in ('binding','program'):
   changed=dict(binding='a',program='b');changed[key]='stale';self.assertTrue(api.content_errors(changed,dict(binding='a',program='b')))
if __name__=='__main__':unittest.main()
