import importlib.util
import pathlib
import struct
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULE = ROOT / 'scripts/cnn/generate_fixtures.py'

class ReferenceTests(unittest.TestCase):
    def load_reference(self):
        self.assertTrue(MODULE.is_file(), 'CNN reference implementation missing')
        spec = importlib.util.spec_from_file_location('cnn_fixtures', MODULE)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_ones_and_negative(self):
        m = self.load_reference()
        weights = {'conv': [1]*18, 'conv_bias': [0,0], 'fc': [1]*36, 'fc_bias': [0,0]}
        out = m.reference([1]*64, weights)
        self.assertEqual(out, {'z':[9]*72, 'r':[9]*72, 'p':[9]*18, 'y':[162,162]})
        out = m.reference([-1]*64, weights)
        self.assertEqual(out, {'z':[-9]*72, 'r':[0]*72, 'p':[0]*18, 'y':[0,0]})

    def test_cross_correlation_layout(self):
        m = self.load_reference()
        x = [0]*64
        x[1*8+2] = 3
        w = {'conv':list(range(1,19)), 'conv_bias':[0,0], 'fc':[0]*36, 'fc_bias':[0,0]}
        out = m.reference(x, w)
        self.assertEqual(out['z'][0], 18)  # ky1,kx2 weight6, no kernel flip
        self.assertEqual(out['z'][36], 45) # output channel1 weight15
        self.assertEqual(out['z'][2*6+2], 0)

    def test_overflow_rejected(self):
        m = self.load_reference()
        w = {'conv':[2147483647]*18,'conv_bias':[0,0],'fc':[1]*36,'fc_bias':[0,0]}
        with self.assertRaises(OverflowError): m.reference([4]*64, w)

    def test_fixture_and_guard_lengths(self):
        m = self.load_reference()
        with tempfile.TemporaryDirectory() as folder:
            for case in ('main','zero','negative','positive','checker'):
                manifest = m.generate_fixture(case, pathlib.Path(folder)/case)
                self.assertEqual(manifest['scope'], 'CPU_REFERENCE_NOT_GPU')
                self.assertEqual(len(manifest['tensors']),9)
                for tensor in manifest['tensors']:
                    p = pathlib.Path(folder)/case/tensor['file']
                    words = list(struct.unpack('<'+'I'*(p.stat().st_size//4),p.read_bytes()))
                    self.assertEqual(len(words)*4, 128+((tensor['payload_bytes']+63)//64)*64)
                    self.assertEqual(words[:16], [0x55aa55aa]*16)
                    self.assertEqual(words[-16:], [0xaa55aa55]*16)
                    self.assertEqual(words[16+tensor['elements']:-16], [0xc0def00d]*(((tensor['payload_bytes']+63)//64)*16-tensor['elements']))

if __name__ == '__main__': unittest.main()
