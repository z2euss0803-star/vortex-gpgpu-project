"""Clock-conditioned cycles and disjoint Linux intervals, never physical metrology."""
import argparse,json,pathlib
def ns(v):
 if type(v) is not int or not 0<=v<2**64:raise ValueError('INVALID_NS')
 return v

def handoff_summary(cpu,detail,resident,submit,evidence,reap,checkpoint,capture,final_read):
 if type(detail) is not dict or set(detail)!={'version','T_roi_begin_call_ns','T_roi_end_call_ns','cpu'} or type(detail['version']) is not int or detail['version']!=1:raise ValueError('HANDOFF_VERSION')
 if type(detail['cpu']) is not list or len(detail['cpu'])!=len(cpu):raise ValueError('HANDOFF_COVERAGE')
 begin=ns(detail['T_roi_begin_call_ns']);end=ns(detail['T_roi_end_call_ns'])
 total=own_checkpoint=input_sha=output_sha=worker_exterior=other=0
 fields={'sequence','stage','T_handler_ns','T_checkpoint_ns','T_input_sha_ns','T_output_sha_ns','T_gelu_call_ns','T_gelu_exterior_ns'}
 for c,d in zip(cpu,detail['cpu']):
  if type(d) is not dict or set(d)!=fields or any(type(d[k]) is not int or d[k]!=c[k] for k in ('sequence','stage')):raise ValueError('HANDOFF_IDENTITY')
  v={k:ns(d[k]) for k in fields-{'sequence','stage'}}
  if v['T_gelu_call_ns']<c['T_compute_ns'] or v['T_gelu_exterior_ns']!=v['T_gelu_call_ns']-c['T_compute_ns']:raise ValueError('HANDOFF_WORKER_SCOPE')
  legacy=sum(ns(c[k]) for k in ('T_begin_call_ns','T_compute_ns','T_commit_call_ns','T_evidence_call_ns','T_writeback_capture_ns','T_cpu_reference_ns'))
  used=legacy+v['T_checkpoint_ns']+v['T_input_sha_ns']+v['T_output_sha_ns']+v['T_gelu_exterior_ns']
  if used>v['T_handler_ns']:raise ValueError('HANDOFF_CPU_OVERLAP')
  total+=v['T_handler_ns'];own_checkpoint+=v['T_checkpoint_ns'];input_sha+=v['T_input_sha_ns'];output_sha+=v['T_output_sha_ns'];worker_exterior+=v['T_gelu_exterior_ns'];other+=v['T_handler_ns']-used
 if own_checkpoint>checkpoint:raise ValueError('HANDOFF_CHECKPOINT_SCOPE')
 used=submit+evidence+reap+total-own_checkpoint+checkpoint+capture+final_read+begin+end
 if used>resident:raise ValueError('HANDOFF_RESIDENT_OVERLAP')
 return dict(handoff_timing_version=1,T_roi_begin_call_ns=begin,T_roi_end_call_ns=end,T_CPU_handler_ns=total,T_CPU_handler_checkpoint_ns=own_checkpoint,T_CPU_input_sha_ns=input_sha,T_CPU_output_sha_ns=output_sha,T_CPU_gelu_exterior_ns=worker_exterior,T_CPU_handler_other_ns=other,T_unclassified_ns=resident-used,handoff_scope='CPU handler includes checkpoint; subtract its checkpoint once before adding global checkpoint; full gelu call includes legacy worker interval')
def timing_summary(gpu,cpu,m,clock):
 if clock!=96969696:raise ValueError('CLOCK_CONTRACT')
 for g in gpu:
  if type(g.get('elapsed_cycles')) is not int or not 0<g['elapsed_cycles']<2**32:raise ValueError('ELAPSED_MISSING_OVERFLOW')
 submit=sum(ns(x['T_submit_wait_ns']) for x in gpu);evidence=sum(ns(x['T_evidence_ns']) for x in gpu);reap=sum(ns(x['T_reap_ns']) for x in gpu)
 cpu_outer=sum(sum(ns(x[k]) for k in ('T_begin_call_ns','T_compute_ns','T_commit_call_ns','T_evidence_call_ns','T_writeback_capture_ns','T_cpu_reference_ns')) for x in cpu)
 resident=ns(m['T_resident_ns']);checkpoint=ns(m['T_checkpoint_ns']);capture=ns(m['T_diagnostic_capture_ns']);final_read=ns(m['T_final_read_ns'])
 if resident>180000000000 or submit+evidence+reap+cpu_outer+checkpoint+capture+final_read>resident:raise ValueError('INTERVAL_SCOPE_OR_DEADLINE')
 cycles=sum(x['elapsed_cycles'] for x in gpu)
 out=dict(raw_gpu_cycles=cycles,configured_clock_hz=clock,gpu_seconds_conditional=cycles/clock,gpu_seconds_physical=None,clock_scope='RUNTIME_PLL_CONFIGURATION_NOT_INDEPENDENT_MEASUREMENT',T_load_ns=ns(m['T_load_ns']),T_resident_ns=resident,T_submit_wait_ns=submit,T_evidence_ns=evidence,T_reap_ns=reap,T_CPU_outer_ns=cpu_outer,T_checkpoint_ns=checkpoint,T_diagnostic_capture_ns=capture,T_final_read_ns=final_read,nested_intervals_additive=False,nested_scope='GPU execution and guard/CDMA/polling are contained within submit/wait; CPU driver subintervals are contained within CPU calls',publish_performance=False)
 if 'timing_extension' in m:out.update(handoff_summary(cpu,m['timing_extension'],resident,submit,evidence,reap,checkpoint,capture,final_read))
 return out
def summarize(run,audit):
 if audit.get('execution')!='PASS':raise ValueError('AUDIT_REQUIRED')
 return dict(scope='FUSED_DIAGNOSTIC_TIMING_NOT_FORMAL' if audit['mode']!='measured' else 'FUSED_GATED_SAMPLE',**audit['timing'],formal_eligible=audit['formal_eligible'],h1='NOT_RUN')
if __name__=='__main__':
 from audit_run import audit_run
 p=argparse.ArgumentParser()
 for k in ('run','release','baseline','out'):p.add_argument('--'+k,type=pathlib.Path,required=True)
 a=p.parse_args();r=summarize(a.run,audit_run(a.run,a.release,a.baseline))
 with a.out.open('x',encoding='utf-8') as f:json.dump(r,f,indent=2);f.write('\n')
