# S8 future field entry — offline document only

Physical signoff: OPEN_NOT_PASS. No board or SD action is authorized by this document.
GPU raw cycles: null because no real board execution has occurred. GPU seconds: null.
Errors: retain the cumulative docs/s8/ERRORS.md and every original failed attempt.

| Evidence layer | Actual state |
|---|---|
| SW | PASS |
| CROSS_DEVICE | PASS |
| CROSS_CPU | PASS |
| CROSS_KERNEL | PRE_BINDING |
| CROSS_BINDING | PASS |
| IMAGE | PASS |
| BOARD_PLATFORM | NOT_RUN |
| BOARD_S8 | NOT_RUN |
| COMPAT | NOT_RUN |
| PERF | NOT_RUN |

Future correctness budget: 203 jobs = identity case1 (1) + compat cases2/3/4/5 (4) + 11 positive cases × 4 cores × 4 owners (176) + 18 independent negative cases (18) + case120 four-owner sweep (4).
The earlier 202-job estimate preceded approved nearest case5. This arithmetic correction retains every required case.
Future order: exact boot/build/binding and transfer identity; CRT64; scalar/WMMA/bilinear/nearest; independent signed INT16/signed48 positives; negatives with sticky error and recovery; four-core contention; then separately authorized flags0/flags1 paired timing.
Use healthy evidence epochs within operations2048 and owner256 capacities. Fail-stop and seal readable data before REAP; unknown in-flight data remain pinned.
Every normal START includes controller initialization/core reset. No extra fault reset, automatic retry, PL reconfiguration or media update is implied.
Boot mode and exact media are for the user to choose later. Present image hashes, current card identity, backups and restoration before a future SD write.
This budget is a plan, not a run or application acceptance. Raw controller cycles, optional44-bit mcycle events and Linux clock-domain intervals remain separate.

Learning question: Why does a correct ELF not establish S8 PASS?
Answer: static compilation closes syntax/link/ISA/stack/TLS gates; only real hardware execution, raw24-page capture,64-hart identity, guards/CRT and independent integer comparison can close BOARD_S8.

Current source: 9c34bfb2e940add9954fd437a2ef535cdc39880bb7c5b784b718ff2577305798 (adafe79).
Current binding: eda1b80f555a33272bacb13b980f0371bed4b24ec72a7081406d67b1c661bb34.
Actual image build ID: S8-LINUX-VALIDATION-V1-20261018T093916Z.
Actual offline image content audit: PASS; board and performance NOT_RUN.
Reproduction requires the independently hashed BSP configuration supplement after software preparation.
