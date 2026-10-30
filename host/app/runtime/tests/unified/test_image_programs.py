import hashlib,json,pathlib,struct,unittest,tempfile,shutil
from scripts.unified.build_image_programs import image_programs
from scripts.unified.build_catalog import build_catalog
ROOT=pathlib.Path(__file__).resolve().parents[2]
class ImagePrograms(unittest.TestCase):
    def test_runtime_header_uses_stable_program_keys_not_catalog_offsets(self):
        header=(ROOT/'generated/image/programs.h').read_text()
        self.assertNotIn('vx_image_entry_id',header,'entry IDs vary with combined catalog layout; resolve full ProgramKey at runtime')
        provider=json.loads((ROOT/'contracts/unified/providers.json').read_bytes())['providers'][0]
        records=image_programs(ROOT/'vendor/image-studio',provider)
        first=records[0];extra=dict(first);extra['fields']=list(first['fields']);extra['fields'][:3]=[1,1,65599]
        standalone=build_catalog(records);combined=build_catalog([extra]+records)
        for i in range(10):
            one=standalone[64+512*i:64+512*(i+1)];two=combined[64+512*(i+1):64+512*(i+2)]
            self.assertEqual(one[:12],two[:12])
            self.assertEqual(one[64:160],two[64:160],'code/readonly/schema SHA stay the same')
            self.assertNotEqual(one[256:288],two[256:288],'actual entry IDs must be resolved again')
    def test_rehashed_replacement_words_are_not_original_delivery(self):
        provider=json.loads((ROOT/'contracts/unified/providers.json').read_bytes())['providers'][0]
        with tempfile.TemporaryDirectory() as directory:
            copy=pathlib.Path(directory);shutil.copytree(str(ROOT/'vendor/image-studio/gpu_programs'),str(copy/'gpu_programs'))
            path=copy/'gpu_programs/manifest.json';m=json.loads(path.read_bytes());gold=m['kernels'][0]
            raw_path=copy/'gpu_programs/gray.bin';raw=bytearray(raw_path.read_bytes());raw[0]^=4;raw_path.write_bytes(raw)
            padded_path=copy/'gpu_programs/gray.padded.bin';padded=bytearray(padded_path.read_bytes());padded[:len(raw)]=raw;padded_path.write_bytes(padded)
            gold['sha256']=hashlib.sha256(raw).hexdigest();gold['padded_sha256']=hashlib.sha256(padded).hexdigest();path.write_text(json.dumps(m))
            with self.assertRaisesRegex(ValueError,'ORIGINAL_MANIFEST_SHA'):image_programs(copy,provider)
    def test_exact_words_startup_schema_and_namespace(self):
        provider=json.loads((ROOT/'contracts/unified/providers.json').read_bytes())['providers'][0]
        records=image_programs(ROOT/'vendor/image-studio',provider)
        self.assertEqual(len(records),10,'all ten original op libraries admitted')
        original=json.loads((ROOT/'vendor/image-studio/gpu_programs/manifest.json').read_bytes())['kernels']
        for op,(record,gold) in enumerate(zip(records,original)):
            code=record['code'];padded=(ROOT/'vendor/image-studio/gpu_programs'/(gold['name']+'.padded.bin')).read_bytes()
            self.assertEqual(len(code),2048)
            self.assertEqual(code[:len(padded)],padded)
            self.assertEqual(code[len(padded):],bytes(2048-len(padded)))
            self.assertEqual(record['digests'][0],hashlib.sha256(code).digest())
            self.assertEqual(record['digests'][7].hex(),gold['sha256'])
            self.assertEqual(record['fields'][:3],[3,256,512+op])
            self.assertEqual(record['fields'][8:11],[0x10000000,0x10010000,64])
            header=struct.unpack('<16I',record['schema'][:64])
            self.assertEqual(header[1],2)
            self.assertEqual(header[5:12],(0x10000000,0,0,0,0,0,1))
            self.assertEqual(code[:24],padded[:24],'hardcoded START not relocated')
            fields=[struct.unpack_from('<16I',record['schema'],64+64*i) for i in range(header[4])]
            table=next((x for x in fields if x[1]==40),None)
            if op in (2,3,4,5,7,9):
                self.assertEqual(table[0],2)
                self.assertEqual(table[3],1,'GPU table read-only ref')
                self.assertEqual(table[9],52,'scalar table suboffset, aligned whole table ref')
        catalog=build_catalog(records)
        self.assertLess(len(catalog),65536)
        self.assertEqual(struct.unpack_from('<I',catalog,12)[0],10)
        print('U14_IMAGE10_EXACT_WORDS2048_OBJECTS_SCHEMAS_PASS_STATIC_NOT_GPU gpu_raw_cycles=null')
if __name__=='__main__':unittest.main()
