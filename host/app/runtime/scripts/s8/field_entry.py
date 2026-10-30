"""Generate reviewable future field scope only; no execution entrypoint."""
import re
from scripts.s8.summary import LAYERS
def render(summary):
 rows=['# S8 future field entry — offline document only','',
 'Physical signoff: OPEN_NOT_PASS. No board or SD action is authorized by this document.',
 'GPU raw cycles: null because no real board execution has occurred. GPU seconds: null.',
 'Errors: retain the cumulative docs/s8/ERRORS.md and every original failed attempt.',
 '', '| Evidence layer | Actual state |','|---|---|']
 rows += ['| '+k+' | '+summary['states'][k]+' |' for k in LAYERS]
 rows += ['', 'Future correctness budget: 203 jobs = identity case1 (1) + compat cases2/3/4/5 (4) + 11 positive cases × 4 cores × 4 owners (176) + 18 independent negative cases (18) + case120 four-owner sweep (4).',
 'The earlier 202-job estimate preceded approved nearest case5. This arithmetic correction retains every required case.',
 'Future order: exact boot/build/binding and transfer identity; CRT64; scalar/WMMA/bilinear/nearest; independent signed INT16/signed48 positives; negatives with sticky error and recovery; four-core contention; then separately authorized flags0/flags1 paired timing.',
 'Use healthy evidence epochs within operations2048 and owner256 capacities. Fail-stop and seal readable data before REAP; unknown in-flight data remain pinned.',
 'Every normal START includes controller initialization/core reset. No extra fault reset, automatic retry, PL reconfiguration or media update is implied.',
 'Boot mode and exact media are for the user to choose later. Present image hashes, current card identity, backups and restoration before a future SD write.',
 'This budget is a plan, not a run or application acceptance. Raw controller cycles, optional44-bit mcycle events and Linux clock-domain intervals remain separate.',
 '', 'Learning question: Why does a correct ELF not establish S8 PASS?',
 'Answer: static compilation closes syntax/link/ISA/stack/TLS gates; only real hardware execution, raw24-page capture,64-hart identity, guards/CRT and independent integer comparison can close BOARD_S8.']
 return '\n'.join(rows)+'\n'
def validate(text,summary):
 if summary.get('physical_status')!='OPEN_NOT_PASS' or 'OPEN_NOT_PASS' not in text:raise ValueError('physical open boundary missing')
 if not all(k in summary['states'] and k in text for k in LAYERS):raise ValueError('missing evidence layers')
 if not all(k in text for k in ('null','no real board execution','ERRORS.md','203','case5','2048','REAP')):raise ValueError('missing field boundary')
 if re.search(r'(?im)^\s*(?:dd\s|mkfs|xsct\s|xsdb\s|bootgen\s|devmem\s|reboot\s|poweroff\s)',text):raise ValueError('executable field/media command')
 return True
