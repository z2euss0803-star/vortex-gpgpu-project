from pathlib import Path
import re,subprocess,unittest
ROOT=Path(__file__).resolve().parents[2]
class DependencyTests(unittest.TestCase):
    def test_runtime_library_requires_s8_objects_before_recipe_expansion(self):
        proc=subprocess.run(['make','-pn','help'],cwd=str(ROOT),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        self.assertEqual(proc.returncode,0,proc.stderr.decode())
        text=proc.stdout.decode()
        line=re.search(r'^build/libvortex-zynq\.so:([^\n]*)',text,re.M)
        self.assertIsNotNone(line)
        self.assertIn('build/core/s8_layout.o',line.group(1))
        self.assertIn('build/core/s8_validate.o',line.group(1))
if __name__=='__main__':unittest.main()
