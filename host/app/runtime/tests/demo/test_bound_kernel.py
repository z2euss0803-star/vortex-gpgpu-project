import pathlib,tempfile,unittest
from scripts.demo.build_kernel import checked_bindings
from scripts.mosaic.binding import make_binding,ORDER
from tests.demo.test_dual_binding import DualBinding

class BoundKernel(unittest.TestCase):
    def test_both_complete_bindings_and_actual_cpu_required(self):
        m=make_binding({n:'01'*32 for n in ORDER});h=DualBinding().old()
        # An old executable or deferred all-zero self slot must never be installed.
        with self.assertRaises(ValueError):checked_bindings(m,h,'02'*32)
        b=bytearray(h);b[256:288]=bytes([1])*32
        self.assertEqual(checked_bindings(m,bytes(b),'01'*32),(m,bytes(b)))
        b=bytearray(m);b[320:352]=bytes(32)
        with self.assertRaises(ValueError):checked_bindings(bytes(b),bytes(h),'01'*32)
        b=bytearray(m);b[12:16]=(5).to_bytes(4,'little')
        with self.assertRaises(ValueError):checked_bindings(bytes(b),bytes(h),'01'*32)

if __name__=='__main__':unittest.main()
