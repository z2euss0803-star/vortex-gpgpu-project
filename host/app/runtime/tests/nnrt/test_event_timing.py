import os
import subprocess
import unittest


class EventTimingTests(unittest.TestCase):
    def check(self,mode):
        binary=os.environ.get('NNRT_TIMING_TEST_BINARY')
        self.assertTrue(binary,'actual C++ timing contract binary required')
        proc=subprocess.run([binary,mode],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        self.assertEqual(proc.returncode,0,proc.stderr.decode('utf-8','replace'))
    def test_offline_raw_cycles_null(self):self.check('offline')
    def test_nested_time_not_double_counted(self):self.check('nested')
    def test_cross_host_clocks_not_subtracted(self):self.check('cross-host')
    def test_file_writer_preserves_integer_and_null(self):self.check('writer')
