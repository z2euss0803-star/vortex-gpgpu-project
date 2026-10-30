import importlib,unittest
from pathlib import Path
class BuildRV32(unittest.TestCase):
 def test_full_sources_and_frozen_sdk_no_tiny_entry(self):
  api=importlib.import_module('scripts.s8.build_rv32')
  for name in ('main.cpp','admission.cpp','protocol.cpp','negative.cpp','compat.cpp','phase.cpp','unpack.cpp'):
   self.assertIn('device/s8/'+name,api.SOURCES)
  for name in ('vx_start.S','vx_spawn.c','vx_syscalls.c'):self.assertIn(name,api.SDK_SOURCES)
  self.assertIn('-march=rv32imaf',api.FLAGS);self.assertIn('-mabi=ilp32f',api.FLAGS)
  self.assertIn('-fstack-usage',api.FLAGS)
if __name__=='__main__':unittest.main()
