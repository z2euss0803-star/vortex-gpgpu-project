"""Parse bytes from the production C serializer, not a Python imitation."""
import csv
import io
from pathlib import Path
import unittest

class Serialization(unittest.TestCase):
    def test_production_snapshot_headers_lengths_and_raw_crt(self):
        root=Path('build/s8')
        self.assertEqual(len((root/'trace-binding.bin').read_bytes()),512)
        tables=[]
        for name in ('jobs','operations'):
            lines=(root/('trace-'+name+'.csv')).read_text().splitlines()
            self.assertTrue(lines[0].startswith('#VXL_S8_TRACE_V1 '))
            for field in ('jobs_capacity=256','operations_capacity=2048','boot=09','active=0','generation='):
                self.assertIn(field,lines[0])
            records=list(csv.reader(io.StringIO('\n'.join(lines[1:]))))
            self.assertEqual(len(set(records[0])),len(records[0]))
            self.assertTrue(all(len(r)==len(records[0]) for r in records[1:]))
            tables.append(list(csv.DictReader(io.StringIO('\n'.join(lines[1:])))))
        self.assertEqual(tables[0][0]['elapsed_raw'],'123456')
        self.assertEqual(tables[0][0]['elapsed_u64'],'123456')
        self.assertEqual(tables[0][0]['jobs_before'],'0')
        self.assertEqual(tables[0][0]['cdma_status'],'4098')
        self.assertEqual(tables[0][0]['elapsed_address'],str(0xa0000028))
        self.assertEqual(len(bytes.fromhex(tables[0][0]['crt_guards'])),192)
        self.assertEqual(tables[1][0]['metric'],'18') # ACK_TIME
        self.assertEqual(tables[1][0]['begin_ns'],'1')
        self.assertEqual(tables[1][0]['end_ns'],'2')

if __name__=='__main__':unittest.main()
