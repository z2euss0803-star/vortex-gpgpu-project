import json,pathlib,subprocess,sys
def main():
 out=pathlib.Path(sys.argv[1]);fixture=json.loads((out/'mosaic-generated/fixture.json').read_text())
 raise SystemExit(subprocess.call([str(out/'test_mosaic_propagation'),fixture['asset']]))
if __name__=='__main__':main()
