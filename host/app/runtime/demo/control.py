from .contracts import hex_id,validate_asset,validate_run
from .transport import ACTIONS


def control(action,request,transport):
    if action not in ACTIONS or not isinstance(request,dict):raise ValueError('fixed action/object required')
    if action=='select-app':
        if request.get('app') not in ('mosaic','hunet'):raise ValueError('fixed application required')
    elif action.startswith('run-'):
        request=validate_run(request)
        if action!='run-'+request['app'] or request['state']!='STAGED':raise ValueError('wrong application or run state')
    elif action in ('status','display','export'):
        hex_id(request.get('run_id'),32)
        if action=='display':hex_id(request.get('source_result_sha'),64)
    elif action=='stage':
        if 'local_path' not in request or 'asset' not in request:raise ValueError('local path and asset required')
        return transport.stage(request['local_path'],validate_asset(request['asset']))
    return transport.call(action,request)
