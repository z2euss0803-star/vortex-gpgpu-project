import json
import re
import unittest
from pathlib import Path
from scripts.s8.catalog import expected_cases
from scripts.s8 import crt
ROOT=Path(__file__).resolve().parents[2]

class Negative(unittest.TestCase):
    def test_exact_18_variants_and_bounded_event_counts(self):
        rows=json.loads((ROOT/'contracts/s8/negative_events.json').read_text())
        self.assertEqual(sorted(r['case_id'] for r in rows),sorted(r['id'] for r in expected_cases() if r['kind']=='negative'))
        self.assertEqual(len(rows),18)
        for r in rows:
            self.assertLessEqual(r['status_count'],16)
            self.assertEqual(r['recovery_k'],1)
            self.assertIn(r['recovery_warp'],('owner','contender'))
            self.assertEqual(r['primary_result'],r['case_id']>>8 not in (201,208,209))
    def test_partial_mask_region_has_no_missing_lane_collective(self):
        text=(ROOT/'device/s8/negative.cpp').read_text()
        body=crt.body(text.replace('static s8_words4 native_partial','int native_partial'),'native_partial')
        asm=re.search(r'__asm__ volatile\((.*?)\);',body,re.S).group(1)
        self.assertIn('li t0, 7',asm);self.assertIn('li t0, 15',asm)
        self.assertLess(asm.index('li t0, 7'),asm.index('x8, x13'))
        self.assertLess(asm.index('sw t1'),asm.index('li t0, 15'))
        self.assertNotRegex(asm,r'vote|shfl|barrier|call')
        self.assertIn('i<3',body)
        self.assertIn('for(uint32_t poll=0;poll<n->job->spin_limit;++poll)',text)
        self.assertIn('phase<=2',text)

if __name__=='__main__':unittest.main()
