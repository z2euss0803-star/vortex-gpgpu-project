import pathlib,tempfile,unittest,json,shutil,hashlib
from scripts.unified.cross_assets import verify_original_assets
ROOT=pathlib.Path(__file__).resolve().parents[2]
def copy_inputs(root):
 names=['docs/unified/U16-inputs.json','docs/unified/U16-oracles.json','generated/p8/assets.h']
 spec=json.loads((ROOT/names[0]).read_text())
 names += [row['path'].replace('\\','/') for group in spec['sources'] for row in group['arrays']]
 names += ['data/p8/oracle-'+row['name']+'.bin' for row in json.loads((ROOT/names[1]).read_text())['oracles']]
 for name in names:
  p=root/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(str(ROOT/name),str(p))
class CrossAssets(unittest.TestCase):
 def test_actual_original_bank_and_oracles_have_separate_byte_only_inventory(self):
  result=verify_original_assets(ROOT);self.assertEqual(result['scope'],'ORIGINAL_ASSET_BYTE_CLOSURE_ONLY_NOT_RENDER_OR_GPU');self.assertIsNone(result['gpu_raw_cycles']);self.assertEqual(len(result['asset_files']),18);self.assertEqual(len(result['oracle_files']),3);self.assertEqual(sum(x['bytes'] for x in result['asset_files']),22115372);self.assertEqual(sum(x['bytes'] for x in result['oracle_files']),538624)
 def test_changed_pose_bytes_cannot_keep_the_frozen_asset_identity(self):
  base=(ROOT/'build/U22-asset-fixtures').resolve();base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(base)) as name:
   root=pathlib.Path(name).resolve();self.assertEqual(root.parent,base);copy_inputs(root);p=root/'data/p8/cm_warrior_poses.bin';raw=bytearray(p.read_bytes());raw[0]^=1;p.write_bytes(raw)
   with self.assertRaisesRegex(ValueError,'asset.*digest'):verify_original_assets(root)
 def test_rehashed_changed_asset_declaration_is_not_a_new_frozen_reference(self):
  base=(ROOT/'build/U22-asset-fixtures').resolve();base.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(dir=str(base)) as name:
   root=pathlib.Path(name).resolve();self.assertEqual(root.parent,base);copy_inputs(root);p=root/'docs/unified/U16-inputs.json';doc=json.loads(p.read_text());doc['sources'][0]['arrays'][0]['sha256']='0'*64;p.write_text(json.dumps(doc))
   with self.assertRaisesRegex(ValueError,'frozen declaration'):verify_original_assets(root)
if __name__=='__main__':unittest.main()
