import struct,unittest
from scripts.demo.provider_header import provider_header,REQUIRED_FILES
from scripts.mosaic.binding import ORDER,make_binding
from tests.demo.test_dual_binding import DualBinding
class Header(unittest.TestCase):
    def test_reject_self_dependent_files_and_unbound_templates(self):
        records={p:dict(bytes=64,sha256='01'*32) for p in REQUIRED_FILES}
        m=bytearray(make_binding({n:'02'*32 for n in ORDER}));m[320:352]=bytes(32)
        h=bytearray(DualBinding().old());h[256:288]=bytes(32)
        make=lambda records,m,h:provider_header(records,bytes(m),bytes(h),'03'*32,'04'*32,'05'*32)
        text=make(records,m,h);self.assertIn('DEMO_RELEASE_FILE_COUNT',text);self.assertIn('demo_release_libm_sha',text)
        bad=dict(records);bad['hunet/cpu/gelu.elf']=dict(bytes=64,sha256='01'*32)
        with self.assertRaises(ValueError):make(bad,m,h)
        bad=dict(records);bad.pop(next(iter(REQUIRED_FILES)))
        with self.assertRaises(ValueError):make(bad,m,h)
        m[320]=1
        with self.assertRaises(ValueError):make(records,m,h)
if __name__=='__main__':unittest.main()
