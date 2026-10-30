import importlib,unittest
class NativeCXX(unittest.TestCase):
 def test_gcc7_adapter_is_host_only_and_links_after_objects(self):
  api=importlib.import_module('scripts.s8.native_cxx')
  command=api.command(['-std=c++17','test.o','-o','test'])
  self.assertEqual(command[0],'/usr/bin/g++');self.assertEqual(command[-1],'-lstdc++fs')
  self.assertIn('-Iscripts/s8/gcc7compat',command)
  self.assertNotIn('-lstdc++fs',api.command(['-c','test.cpp']))
if __name__=='__main__':unittest.main()
