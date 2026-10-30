"""Remove each production phase fence or global join and require runtime rejection."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class Mutations(unittest.TestCase):
    def test_each_phase_requires_both_orders(self):
        source=(ROOT/'device/hunet_opt_gn_runtime.h').read_text()
        chunks=source.split('opt_gn_join(p);');self.assertEqual(len(chunks),7)
        parent=ROOT/'build/opt-gn-mutations';parent.mkdir(exist_ok=True)
        run=Path(tempfile.mkdtemp(dir=str(parent)))
        for phase in range(6):
            for missing,replacement in [('fence','p.barrier(0x80000000,4);'),('barrier','p.fence();')]:
                case=run/('phase%d-missing-%s'%(phase,missing));case.mkdir()
                modified=chunks[0]
                for i,part in enumerate(chunks[1:]):modified+=(replacement if i==phase else 'opt_gn_join(p);')+part
                header=case/'mutated.h';header.write_text(modified)
                exe=case/'test'
                command=[os.environ['OPT_CXX'],'-std=c++17','-O2','-fno-fast-math','-ffp-contract=off','-pthread','-I'+str(ROOT/'device'),'-DGN_PHASE_HEADER="'+str(header)+'"',str(ROOT/'tests/hunet_opt/test_gn_phases.cpp'),'-o',str(exe)]
                build=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                (case/'compile.stdout').write_bytes(build.stdout);(case/'compile.stderr').write_bytes(build.stderr)
                self.assertEqual(build.returncode,0,build.stderr.decode())
                result=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=8)
                (case/'stdout').write_bytes(result.stdout);(case/'stderr').write_bytes(result.stderr)
                (case/'receipt.json').write_text(json.dumps(dict(scope='HOST_PHASE_MUTATION_NOT_GPU',command=command,natural_exit=result.returncode))+'\n')
                self.assertNotEqual(result.returncode,0)
        print('TWELVE_ACTUAL_PHASE_FENCE_OR_BARRIER_MUTATIONS_REJECTED_NOT_GPU')
if __name__=='__main__':unittest.main()
