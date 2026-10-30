import unittest
from scripts.demo.resources import admit_resources
from scripts.demo.ubuntu import run_checked
G=1024**3
def env():
    return dict(host='z2euss-virtual-machine',account='z2euss',source_identity_verified=True,available_ram=8*G,free_disk=20*G,locks_owned=True,other_heavy=[])
class ResourceTests(unittest.TestCase):
    def test_resource_refusal_does_not_spawn(self):
        for key,value in [('available_ram',G-1),('free_disk',2*G-1),('locks_owned',False),('host','unknown'),('account','root'),('source_identity_verified',False),('available_ram',None)]:
            d=env();d[key]=value;calls=[]
            r=run_checked(d,'unit',['make','-j1','test'],lambda cmd:calls.append(cmd))
            self.assertEqual(r['state'],'RESOURCE_REFUSED');self.assertEqual(calls,[])
    def test_fresh_heavy_and_image_gates(self):
        for key,value in [('available_ram',4*G-1),('other_heavy',['C bitbake'])]:
            d=env();d[key]=value
            with self.assertRaises(ValueError):admit_resources(d,'sanitize')
        d=env();d['free_disk']=15*G-1
        with self.assertRaises(ValueError):admit_resources(d,'image')
        self.assertEqual(admit_resources(env(),'unit')['worker_count'],1)
    def test_failure_and_missing_semantics_never_pass(self):
        for code,out in [(1,b'PASS'),(0,b'no marker'),(0,b'EXPECTED_PASS\nAddressSanitizer: ERROR')]:
            r=run_checked(env(),'unit',['make','-j1','test'],lambda cmd:(code,out),required_marker=b'EXPECTED_PASS')
            self.assertEqual(r['state'],'FAILED')
        r=run_checked(env(),'unit',['make','-j1','test'],lambda cmd:(0,b'EXPECTED_PASS'),required_marker=b'EXPECTED_PASS')
        self.assertEqual(r['state'],'SOFTWARE_COMMAND_PASS_NOT_BOARD')
if __name__=='__main__':unittest.main()
