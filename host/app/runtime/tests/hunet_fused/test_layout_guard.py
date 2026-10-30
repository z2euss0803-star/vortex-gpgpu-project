"""Exercise actual core guard IO; do not emulate its validation logic."""
import pathlib
import subprocess
import tempfile
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]


class LayoutGuards(unittest.TestCase):
    def test_program9_internal_guards_and_unchanged_program8(self):
        temporary=ROOT/'build/test-tmp';temporary.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=str(temporary)) as directory:
            binary=pathlib.Path(directory)/'guards'
            compile_result=subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Iinclude',
                'core/hunet_layout.c','core/hunet_opt_evidence.c','-ffunction-sections','-Wl,--gc-sections','core/pool.c','core/range.c','tests/hunet_fused/test_layout_guard.c',
                '-o',str(binary)],cwd=str(ROOT),stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            self.assertEqual(compile_result.returncode,0,compile_result.stdout.decode())
            run=subprocess.run([str(binary)],cwd=str(ROOT),stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            self.assertEqual(run.returncode,0,run.stdout.decode())
            self.assertIn(b'FUSED_INTERNAL_GUARDS_AND_OLD_MODE_PRESERVED_PASS_NOT_BOARD',run.stdout)


if __name__=='__main__':unittest.main()
