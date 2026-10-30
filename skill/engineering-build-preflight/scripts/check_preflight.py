"""Evaluate declared build admission policy; never sample, launch or stop processes."""
import argparse,json
from decimal import Decimal,InvalidOperation
from pathlib import Path

def amount(value):
    if isinstance(value,bool) or not isinstance(value,(int,float,str)):raise ValueError('numeric quantity required')
    try:n=Decimal(str(value))
    except InvalidOperation:raise ValueError('invalid numeric quantity')
    if not n.is_finite() or n<0:raise ValueError('finite nonnegative quantity required')
    return n

def evaluate(d):
    def result(decision,reason,original='UNKNOWN'):
        return dict(decision=decision,original_decision=original,reason=reason,scope='DECLARED_SNAPSHOT_ONLY',process_operations=False)
    try:
        if not isinstance(d,dict):raise ValueError('object required')
        for k in ['stage','host','account']:
            if not isinstance(d.get(k),str) or not d[k].strip():raise ValueError(k+' required')
        if any(d.get(k) is not True for k in ['tools_known','fresh','sample_after_environment_start']):return result('UNKNOWN','tools, freshness and post-start sampling must be known true')
        if type(d.get('heavy_jobs')) is not int or d['heavy_jobs']<0:raise ValueError('known nonnegative heavy_jobs required')
        policy=d.get('policy');checks=d.get('checks')
        if not isinstance(policy,dict) or not isinstance(checks,dict):raise ValueError('policy and checks required')
        required=policy.get('required_checks')
        if not isinstance(required,list) or any(not isinstance(x,str) or not x.strip() for x in required) or len(set(required))!=len(required):raise ValueError('unique required_checks list required')
        physical=amount(d.get('physical_gib'));commit=amount(d.get('commit_gib'))
        min_physical=amount(policy.get('physical_gib'));min_commit=amount(policy.get('commit_gib'))
        original='ALLOW' if physical>=min_physical and commit>=min_commit else 'REFUSED'
        if d['heavy_jobs']!=0:return result('BLOCKED','other heavy work is active',original)
        if any(checks.get(k)!='PASS' for k in required):return result('BLOCKED','required stage evidence is not PASS',original)
        if original=='ALLOW':return result('ALLOW','declared normal resource policy satisfied',original)
        exception=d.get('exception')
        if (not isinstance(exception,dict) or exception.get('authorized') is not True
            or exception.get('stage')!=d['stage'] or exception.get('host')!=d['host']
            or not isinstance(exception.get('authorization_source'),str) or not exception['authorization_source'].strip()):
            return result('BLOCKED','normal threshold refused; no matching authorized exception',original)
        if physical<amount(exception.get('physical_gib')) or commit<amount(exception.get('commit_gib')):
            return result('BLOCKED','authorized exception threshold also refused',original)
        return result('ALLOW_EXCEPTION','USER_AUTHORIZED_MEMORY_EXCEPTION; normal refusal retained',original)
    except (ValueError,TypeError) as e:return result('UNKNOWN',str(e))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',required=True);a=p.parse_args()
    try:
        with Path(a.input).open(encoding='utf-8-sig') as f:r=evaluate(json.load(f))
    except (OSError,ValueError) as e:r=dict(decision='UNKNOWN',reason=str(e),process_operations=False)
    print(json.dumps(r,ensure_ascii=False,indent=2));return 0

if __name__=='__main__':raise SystemExit(main())
