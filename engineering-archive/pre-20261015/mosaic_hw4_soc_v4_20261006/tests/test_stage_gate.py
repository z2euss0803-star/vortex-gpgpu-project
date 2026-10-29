from pathlib import Path
import importlib.util, unittest
p=Path(__file__).resolve().parents[1]/'scripts/prepare.py'
s=importlib.util.spec_from_file_location('prepare_under_test',p)
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class StageGate(unittest.TestCase):
    def test_exit_zero_runtime_error_is_rejected(self):
        self.assertFalse(m.stage_ok(0,'  Error: runtime error: counter overflow\n'))
    def test_critical_warning_is_rejected(self):
        self.assertFalse(m.stage_ok(0,'CRITICAL WARNING: unresolved clock\n'))
    def test_nonzero_exit_is_rejected(self):
        self.assertFalse(m.stage_ok(1,'PASS\n'))
    def test_ordinary_warning_stays_visible_without_being_promoted_to_error(self):
        self.assertTrue(m.stage_ok(0,'Warning: DSP OPMODE uninitialized\n'))
if __name__=='__main__':unittest.main()
