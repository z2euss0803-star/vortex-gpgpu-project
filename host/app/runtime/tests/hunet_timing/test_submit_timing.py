import pathlib,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]

class SubmitTiming(unittest.TestCase):
    def test_real_guard_success_and_failure_keep_original_contract(self):
        self.assertTrue((ROOT/'include/vxl/submit_timing.h').exists(),'missing approved per-job timing implementation')
        with tempfile.TemporaryDirectory() as d:
            exe=pathlib.Path(d)/'guards'
            subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),
                str(ROOT/'core/hunet_evidence.c'),str(ROOT/'core/hunet_sha256.c'),str(ROOT/'core/digest.c'),str(ROOT/'core/hunet_fused_validate.c'),
                str(ROOT/'tests/hunet_timing/test_guards.c'),'-ffunction-sections','-Wl,--gc-sections','-o',str(exe)],check=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
            self.assertIn('TIMING_REAL_GUARD_SUCCESS_FAILURE_AND_BOUNDS_PASS_NOT_BOARD',result.stdout)

if __name__=='__main__':unittest.main()
