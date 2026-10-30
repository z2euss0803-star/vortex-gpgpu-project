import argparse,hashlib,json,pathlib,platform,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--offline',action='store_true');p.parse_args()
    profile=json.loads((ROOT/'contracts/profiles/h0-r4.json').read_text())
    digest=hashlib.sha256((ROOT/'vendor/h0/gpgpu_with_bit.xsa').read_bytes()).hexdigest()
    result={'host':platform.node(),'system':platform.platform(),'cwd':str(ROOT),'xsa_matches':digest==profile['xsa_sha256'],
            'contract_status':profile['status'],'target_toolchain_locked':(ROOT/'toolchain.lock.json').exists(),
            'external_gates':['D1 H1 manifest/acceptance','D2 reserved PS placement and real master reachability','D3 FAULT reset unproven','D4 generic CRT/program approval (exact r4 diagnostic separately reviewed)','D5 deployment authorization'],
            'board':'POWERED_OFF_NOT_PROBED'}
    print(json.dumps(result,ensure_ascii=False,indent=2))
    return 0 if result['xsa_matches'] else 1
if __name__=='__main__':raise SystemExit(main())
