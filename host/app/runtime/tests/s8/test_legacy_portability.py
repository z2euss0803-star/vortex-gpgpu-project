import unittest
from unittest.mock import patch
from pathlib import Path
from scripts.hunet import package_release as api

class LegacyPortability(unittest.TestCase):
 def test_native_ubuntu_archive_refuses_without_wsl_dependency(self):
  with patch.object(api.os,'name','posix'),patch.object(api.subprocess,'run') as run,patch.object(api.subprocess,'check_output') as convert:
   run.return_value.returncode=128;run.return_value.stderr=b'not a repository'
   with self.assertRaisesRegex(ValueError,'GIT:'):
    api.git(Path('.'),'rev-parse','HEAD')
   convert.assert_not_called()
   self.assertEqual(run.call_args[0][0][0],'git')
if __name__=='__main__':unittest.main()
