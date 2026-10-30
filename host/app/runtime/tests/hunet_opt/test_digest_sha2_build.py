"""Compilation policy only; dedicated target execution is reported separately."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
class Build(unittest.TestCase):
    def test_isolated_crypto_object(self):
        text=(ROOT/'Makefile').read_text()
        self.assertIn('$(OUT)/userspace/digest_sha2_block.o:',text)
        self.assertEqual(text.count('-march=armv8-a+crypto'),1)
        dispatch=(ROOT/'userspace/digest_dispatch.c').read_text()
        self.assertIn('getauxval(AT_HWCAP)',dispatch)
        self.assertIn('HWCAP_SHA2',dispatch)
        self.assertNotIn('arm_neon.h',dispatch)
        self.assertNotIn('arm_neon.h',(ROOT/'core/digest.c').read_text())
if __name__=='__main__':unittest.main()
