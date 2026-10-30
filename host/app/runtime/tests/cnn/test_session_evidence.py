"""Check saved TEST_PROTOCOL records, never accept them as board evidence."""
import json
import os
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/cnn'))
from summarize import summarize
class Saved(unittest.TestCase):
    def test_saved_files(self):
        out=ROOT/os.environ.get('CNN_OUT','build/cnn')
        folder=max((out/'session-evidence').iterdir(),key=lambda p:int(p.name))
        run=json.loads((folder/'run.json').read_text());self.assertEqual(run['scope'],'TEST_PROTOCOL_NOT_GPU')
        self.assertEqual(run['result'],0);self.assertEqual(run['save_result'],0);self.assertEqual(run['close_result'],0)
        records=[json.loads(l) for l in (folder/'timing.jsonl').read_text().splitlines()]
        summary=summarize(records);self.assertTrue(summary['complete'],summary['errors']);self.assertFalse(summary['real_board_pass'])
        programs=json.loads((ROOT/'kernel/cnn/manifest.json').read_text())['programs']
        outputs=('z','r','p','y');fixture=Path(os.environ.get('CNN_FIXTURES',str(out/'fixtures')))/'main'
        names=('x','wconv','bconv','z','r','p','wfc','bfc','y')
        for i,r in enumerate(records):
            self.assertEqual(len(r['transfers']),45)
            for stage,l in enumerate(r['layers']):
                self.assertEqual(l['program_sha256'],programs[stage]['sha256'])
                self.assertEqual(l['snapshot']['job_id'],l['job_id'])
                self.assertEqual(l['job_id'],i*4+stage+1)
                for j,name in enumerate(names):
                    suffix='.expected.bin' if name in outputs[:stage+1] else '.bin'
                    actual=(folder/f'raw/iteration-{i}-layer-{stage}-object-{j}.bin').read_bytes()
                    self.assertEqual(actual,(fixture/(name+suffix)).read_bytes())
        self.assertEqual(len(list((folder/'raw').glob('*.bin'))),864)
if __name__=='__main__':unittest.main()
