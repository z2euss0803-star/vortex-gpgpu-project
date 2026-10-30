"""B controller. Offline fixture mode has no SSH or hardware side effects."""
import argparse,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from demo.control import control
from demo.transport import DemoTransport,ACTIONS


def main():
    p=argparse.ArgumentParser();p.add_argument('--config',required=True);p.add_argument('--action',choices=ACTIONS,required=True)
    p.add_argument('--request');p.add_argument('--offline-fixture');p.add_argument('--output');args=p.parse_args()
    if args.offline_fixture:
        if args.action not in ('identity','status'):raise ValueError('offline fixture only identity/status')
        result=json.loads((Path(args.offline_fixture)/(args.action+'.json')).read_text(encoding='utf-8'))
        if result.get('scope')!='MOCK' or result.get('raw_cycles') is not None:raise ValueError('invalid offline fixture')
    else:
        config=json.loads(Path(args.config).read_text(encoding='utf-8'))
        request=json.loads(Path(args.request).read_text(encoding='utf-8')) if args.request else {}
        transport=DemoTransport(config)
        if args.action=='export':
            if not args.output:raise ValueError('export requires new --output directory')
            result=transport.export_run(request,args.output)
        else:result=control(args.action,request,transport)
    print(json.dumps(result,ensure_ascii=False));return 0


if __name__=='__main__':
    try:raise SystemExit(main())
    except (ValueError,RuntimeError) as error:print(str(error),file=sys.stderr);raise SystemExit(1)
