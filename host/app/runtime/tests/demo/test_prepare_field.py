import pathlib,tempfile,unittest
from unittest.mock import patch
from scripts.demo.prepare_field import prepare_field
from scripts.demo.release import release_files
ROOT=pathlib.Path(__file__).resolve().parents[2]
class FieldTests(unittest.TestCase):
    def test_prepare_never_connects_and_no_image_is_not_ready(self):
        with tempfile.TemporaryDirectory(dir=str(ROOT/'build/demo')) as d:
            root=pathlib.Path(d);source=root/'source';source.write_bytes(b'old missing SUBMIT_ENTER failure retained')
            states=dict(OFFLINE='PARTIAL',CROSS='PARTIAL',IMAGE='NOT_BUILT',BOARD_INT16='NOT_RUN',BOARD_HUNET='NOT_RUN',DP_PHYSICAL='NOT_RUN',PERFORMANCE='NOT_RUN',raw_gpu_cycles=None)
            release_files({'protected-failure.txt':source},states,root/'release')
            with patch('socket.socket',side_effect=AssertionError('no network')),patch('subprocess.Popen',side_effect=AssertionError('no commands')):
                r=prepare_field(root/'release',root/'field')
            self.assertEqual(r['state'],'NOT_READY_FOR_SD');self.assertTrue(r['missing'])
            self.assertEqual(source.read_bytes(),b'old missing SUBMIT_ENTER failure retained')
            self.assertIn('NOT_RUN',(root/'field/FIELD_CHECKLIST.md').read_text())
if __name__=='__main__':unittest.main()
