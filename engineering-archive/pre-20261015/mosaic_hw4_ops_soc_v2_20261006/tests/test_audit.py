import importlib.util,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import audit_ops
from audit_ops import numeric
class TestAudit(unittest.TestCase):
    def test_matching_raw_value(self):
        self.assertEqual(numeric({0x81000040:0x3f800000},{0x81000040:0x3f800000})['max_abs'],0)
    def test_wrong_address_rejected(self):
        with self.assertRaises(AssertionError):numeric({4:0x3f800000},{8:0x3f800000})
    def test_nan_rejected(self):
        with self.assertRaises(AssertionError):numeric({4:0x7fc00000},{4:0x3f800000})
    def test_excess_error_rejected(self):
        with self.assertRaises(AssertionError):numeric({4:0x40000000},{4:0x3f800000})
    def test_unarmed_checker_rejected_despite_end_marker(self):
        check=audit_ops.require_armed_checker
        with self.assertRaises(AssertionError):check('MOSAIC_HW4_ACTUAL_SOC_TB_PASS\n')
    def test_duplicate_armed_marker_rejected(self):
        check=audit_ops.require_armed_checker
        with self.assertRaises(AssertionError):check('HW4_CHECKER_ARMED_PASS ic=1 dc=1\n'*2)
    def test_qualified_armed_marker_accepted(self):
        check=audit_ops.require_armed_checker
        check('HW4_CHECKER_ARMED_PASS ic=1 dc=1\n')
if __name__=='__main__':unittest.main()
