"""Run portable skill tests with this Python interpreter. No device/network access."""
import os,pathlib,sys,unittest
def main():
 root=pathlib.Path(__file__).resolve().parent
 loader=unittest.TestLoader();suite=unittest.TestSuite()
 for folder in sorted(root.iterdir()):
  if (folder/'SKILL.md').is_file() and (folder/'tests').is_dir():suite.addTests(loader.discover(str(folder/'tests'),pattern='test_*.py',top_level_dir=str(folder/'tests')))
 suite.addTests(loader.discover(str(root/'tests'),pattern='test_*.py',top_level_dir=str(root/'tests')))
 result=unittest.TextTestRunner(verbosity=2).run(suite)
 return 0 if result.wasSuccessful() else 1
if __name__=='__main__':raise SystemExit(main())
