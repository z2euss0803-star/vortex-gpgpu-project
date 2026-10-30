"""Reproduce frozen C uint32 arrays/oracle byte extraction; no render acceptance.
Run with an original vortex23-warrior-w4 source root and its Linux frame bundle.
Default audits existing candidate; --out writes into a NEW directory only.
"""
import argparse,hashlib,json,pathlib,re,struct

def sha(raw):return hashlib.sha256(raw).hexdigest()
def main():
 a=argparse.ArgumentParser();a.add_argument('original_root');a.add_argument('frame_bundle');a.add_argument('--out');args=a.parse_args()
 root=pathlib.Path(__file__).resolve().parents[2];source=pathlib.Path(args.original_root);spec=json.loads((root/'docs/unified/U16-inputs.json').read_bytes());oracle=json.loads((root/'docs/unified/U16-oracles.json').read_bytes());out=pathlib.Path(args.out) if args.out else None
 if out:out.mkdir(parents=True,exist_ok=False)
 count=total=0
 for group in spec['sources']:
  raw=(source/group['source']).read_bytes();assert len(raw)==group['source_bytes'] and sha(raw)==group['source_sha256'];text=raw.decode('ascii')
  for row in group['arrays']:
   pattern=r'static\s+const\s+uint32_t\s+'+re.escape(row['symbol'])+r'\s*\[(\d+)\]\s*=\s*\{([^}]+)\}\s*;'
   matches=list(re.finditer(pattern,text,re.S));assert len(matches)==1;match=matches[0];words=[int(token.strip().rstrip('uUlL'),0) for token in match.group(2).split(',') if token.strip()];assert len(words)==int(match.group(1))==row['words']
   data=struct.pack('<%dI'%len(words),*words);assert len(data)==row['bytes'] and sha(data)==row['sha256'];target=root/pathlib.PurePosixPath(row['path'].replace('\\','/'));assert target.read_bytes()==data
   if out:(out/(row['symbol']+'.bin')).write_bytes(data)
   count+=1;total+=len(data)
 frame=pathlib.Path(args.frame_bundle).read_bytes();assert sha(frame)==oracle['bundle_sha256']
 for row in oracle['oracles']:
  data=frame[row['source_offset']:row['source_offset']+row['bytes']];assert len(data)==row['bytes'] and sha(data)==row['sha256'];assert (root/'data/p8'/('oracle-'+row['name']+'.bin')).read_bytes()==data
  if out:
   (out/'oracle').mkdir(exist_ok=True);(out/'oracle'/(row['name']+'.bin')).write_bytes(data)
 print(json.dumps(dict(scope='EXACT_FROZEN_LE_ARRAY_AND_ORACLE_EXTRACTION_NOT_GPU',arrays=count,bytes=total,oracles=len(oracle['oracles']),gpu_raw_cycles=None)))
if __name__=='__main__':main()
