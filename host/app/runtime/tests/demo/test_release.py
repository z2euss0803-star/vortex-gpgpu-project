import pathlib,tempfile,unittest,json,zipfile
from scripts.demo.release import release_files
from scripts.demo.verify_release import verify_release
ROOT=pathlib.Path(__file__).resolve().parents[2]
class ReleaseTests(unittest.TestCase):
    def fixture(self,d):
        source=pathlib.Path(d)/'source.txt';source.write_bytes(b'unique failed evidence')
        states=dict(OFFLINE='PARTIAL',CROSS='PARTIAL',IMAGE='NOT_BUILT',BOARD_INT16='NOT_RUN',BOARD_HUNET='NOT_RUN',DP_PHYSICAL='NOT_RUN',PERFORMANCE='NOT_RUN',raw_gpu_cycles=None)
        return {'evidence/failure.txt':source},states
    def test_offline_cannot_promote_board(self):
        with tempfile.TemporaryDirectory(dir=str(ROOT/'build/demo')) as d:
            files,states=self.fixture(d);states['BOARD_INT16']='PASS'
            with self.assertRaises(ValueError):release_files(files,states,pathlib.Path(d)/'out')
            states['BOARD_INT16']='NOT_RUN';states['raw_gpu_cycles']=127273
            with self.assertRaises(ValueError):release_files(files,states,pathlib.Path(d)/'out')
    def test_archive_and_snapshot_are_rehashed(self):
        with tempfile.TemporaryDirectory(dir=str(ROOT/'build/demo')) as d:
            files,states=self.fixture(d);out=pathlib.Path(d)/'out';release_files(files,states,out)
            self.assertEqual(verify_release(out)['state'],'VERIFIED_PARTIAL_SOFTWARE_NOT_READY_FOR_SD')
            target=out/'files/evidence/failure.txt';original=target.read_bytes();target.write_bytes(b'changed')
            with self.assertRaises(ValueError):verify_release(out)
            target.write_bytes(original)
            with zipfile.ZipFile(out/'software-partial.zip','w') as z:z.writestr('DELIVERY.json','{}')
            with self.assertRaises(ValueError):verify_release(out)
if __name__=='__main__':unittest.main()
