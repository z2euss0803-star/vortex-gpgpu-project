import hashlib,json,pathlib,tempfile,unittest
from scripts.demo.system_release import validate_system_states,release_system,runtime_errors
class SystemRelease(unittest.TestCase):
    def test_old_weights_or_viewer_cannot_be_rehashed_into_new_package(self):
        with tempfile.TemporaryDirectory(dir=str(pathlib.Path(__file__).resolve().parents[2]/'build/demo')) as d:
            base=pathlib.Path(d);(base/'runtime').mkdir();rows={};members={}
            for name,data in [('mosaic/weights.ms8',b'new weights'),('bin/vxl_dp_viewer',b'new viewer')]:
                members[name]=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest());rows['runtime/'+name]=dict(members[name])
            raw=json.dumps(dict(files=members)).encode();(base/'runtime/release-manifest.json').write_bytes(raw);digest=hashlib.sha256(raw).hexdigest()
            self.assertEqual(runtime_errors(base,rows,digest),[])
            for name in members:
                bad={n:dict(r) for n,r in rows.items()};bad['runtime/'+name]['sha256']=hashlib.sha256(b'old file').hexdigest()
                self.assertTrue(runtime_errors(base,bad,digest))
            self.assertTrue(runtime_errors(base,rows,'0'*64))
    def test_image_and_cross_must_be_complete_board_never_inherited(self):
        s=dict(OFFLINE='UBUNTU_SOFTWARE_PASS',CROSS='CROSS_PASS_NOT_BOARD',IMAGE='ACTUAL_DEMO_IMAGE_PASS_NOT_DISPLAY_NOT_BOARD',BOARD_INT16='NOT_RUN',BOARD_HUNET='NOT_RUN',DP_PHYSICAL='NOT_RUN',PERFORMANCE='NOT_RUN',raw_gpu_cycles=None)
        validate_system_states(s)
        for key,value in (('IMAGE','NOT_BUILT'),('CROSS','PARTIAL'),('BOARD_INT16','PASS'),('DP_PHYSICAL','PASS'),('raw_gpu_cycles',127273)):
            bad=dict(s);bad[key]=value
            with self.assertRaises(ValueError):validate_system_states(bad)
    def test_partial_files_cannot_be_promoted(self):
        s=dict(OFFLINE='UBUNTU_SOFTWARE_PASS',CROSS='CROSS_PASS_NOT_BOARD',IMAGE='ACTUAL_DEMO_IMAGE_PASS_NOT_DISPLAY_NOT_BOARD',BOARD_INT16='NOT_RUN',BOARD_HUNET='NOT_RUN',DP_PHYSICAL='NOT_RUN',PERFORMANCE='NOT_RUN',raw_gpu_cycles=None)
        with tempfile.TemporaryDirectory(dir=str(pathlib.Path(__file__).resolve().parents[2]/'build/demo')) as d:
            with self.assertRaises(ValueError):release_system({},s,pathlib.Path(d)/'out')
            self.assertFalse((pathlib.Path(d)/'out').exists())
if __name__=='__main__':unittest.main()
