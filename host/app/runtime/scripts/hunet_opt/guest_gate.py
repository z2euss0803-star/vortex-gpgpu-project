"""Fail-closed admission; a refusal is not a compiler failure."""
def guest_gate(snapshot,coordination):
 try:
  s,c=snapshot,coordination
  checks=dict(host=s['host']=='z2euss-virtual-machine' and s['uid']==1000,
   coordination=c['known'] is True and c['c_heavy'] is False and c['scope']=='HUNET_OFFLINE_ONLY',
   measurements=s['measurement_ok'] is True,memory=s['mem_available']>=4*1024**3,
   disk=s['free_bytes']>=max(15*1024**3,s['estimated_bytes']+s['margin_bytes']),
   idle=not s['heavy'],sdk=s['sdk_target']=='aarch64-xilinx-linux' and s['sdk_version']=='9.2.0',
   kernel=s['kernel_release'].startswith('5.4.'))
  return dict(admission='PASS' if all(checks.values()) else 'REFUSED',checks=checks,scope='UBUNTU_OFFLINE_ONLY',jobs=2,board='NOT_RUN',sd='NOT_TOUCHED')
 except (KeyError,TypeError,ValueError) as e:return dict(admission='REFUSED',reason='MISSING_OR_INVALID_MEASUREMENT:'+str(e))
