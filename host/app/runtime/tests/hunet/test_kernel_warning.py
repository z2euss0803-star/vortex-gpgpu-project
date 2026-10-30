"""Exercise the actual core TU with the kernel's strict const-table warning."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]


class KernelWarnings(unittest.TestCase):
    def test_shared_program_tables_keep_w1_werror(self):
        temp=ROOT/'build/test-tmp';temp.mkdir(exist_ok=True,parents=True)
        with tempfile.TemporaryDirectory(dir=temp) as directory:
            for source in sorted((ROOT/'core').glob('hunet_*.c')):
                with self.subTest(source=source.name):
                    result=subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror',
                        '-Wunused-const-variable=2','-Wdeclaration-after-statement','-I'+str(ROOT/'include'),'-c',str(source),
                        '-o',str(Path(directory)/(source.stem+'.o'))],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                    self.assertEqual(result.returncode,0,result.stdout.decode(errors='replace'))


if __name__=='__main__':unittest.main()
