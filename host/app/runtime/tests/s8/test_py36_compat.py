import subprocess,unittest
from scripts.s8.py36_compat import translate_run,translate_text,timespec_ns
class Compatibility(unittest.TestCase):
 def test_capture_text_keywords_preserve_validation(self):
  self.assertEqual(translate_run(dict(capture_output=True)),dict(stdout=subprocess.PIPE,stderr=subprocess.PIPE))
  with self.assertRaises(ValueError):translate_run(dict(capture_output=True,stdout=None))
  self.assertEqual(translate_text(dict(text=True)),dict(universal_newlines=True))
  with self.assertRaises(subprocess.SubprocessError):translate_text(dict(text=True,universal_newlines=False))
 def test_timespec_is_integer_not_float_conversion(self):
  self.assertEqual(timespec_ns(9999999999,123456789),9999999999123456789)
  with self.assertRaises(ValueError):timespec_ns(1,1000000000)
if __name__=='__main__':unittest.main()
