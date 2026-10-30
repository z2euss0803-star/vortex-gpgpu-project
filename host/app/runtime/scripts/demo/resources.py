"""Fail-closed fresh per-host capacity admission; never deletes engineering files."""
GIB=1024**3
def admit_resources(env, kind):
    if kind not in ('unit','sanitize','numeric21','cross','image'):raise ValueError('unknown resource kind')
    if env.get('host')!='z2euss-virtual-machine' or env.get('account')!='z2euss' or env.get('source_identity_verified') is not True:
        raise ValueError('B Ubuntu identity/source not bound')
    ram,disk=env.get('available_ram'),env.get('free_disk')
    if type(ram) is not int or type(disk) is not int or ram<0 or disk<0:raise ValueError('capacity unknown')
    if env.get('locks_owned') is not True or not isinstance(env.get('other_heavy'),list):raise ValueError('coordination unknown or locks refused')
    if env['other_heavy']:raise ValueError('another heavy task active')
    need_ram=GIB if kind=='unit' else 4*GIB
    need_disk=15*GIB if kind=='image' else 2*GIB
    if ram<need_ram or disk<need_disk:raise ValueError('insufficient fresh RAM/disk')
    return dict(state='RESOURCE_ADMITTED',kind=kind,worker_count=1,available_ram=ram,free_disk=disk,required_ram=need_ram,required_disk=need_disk)
