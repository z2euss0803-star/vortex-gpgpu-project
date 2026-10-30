# Timing and candidate audit contract

A real program10 run requires the exact v4 release, current trusted serial/SSH preflight, boot ID/build/kernel/source/bit/XSA/binding, catalog128B jobs, compact GPU448B and CPU712B records, immutable artifact hashes, CPU runtime/libm, final input/logits/END and driver capture epoch. Host software artifacts never enter this tier.

Default inference does not collect stage tensors, CPU single-thread references, or extra writeback tensors. `diagnostic` captures only materialized graph stages; virtual SLICE/CONCAT stages remain explicitly virtual. CPU evidence stays384B and all checks remain mandatory.

Controller-only supplementary files are `management.csv`, `submit.csv`, `driver-capture.json`. The capture manifest binds SHA/size of both CSVs to run ID, generation and boot ID; their hashes are checked separately from the CLI artifact manifest. Required management roles are SEAL17/token0, ROI BEGIN13/token0, each CPU BEGIN18/token and COMMIT19/token, END21/finaltoken. BEGIN row token is finalized after its locked handler creates the transaction.

Management ns[] are mutually exclusive. Nested CDMA total/wait/short-poll/sleep are contained activity and never added again. Submit timeline partitions are guard init, pre-start handoff, observed GPU activity, guard check, post-done handoff. State polling lies inside observed activity; wait sleep and worker activity can overlap. Raw GPU cycles are retained per physical job and actual operator; configured96,969,696Hz conversion remains conditional and is not physical metrology.

For the current source, one program10 submit has349 PL guard transfers, or351 when parallel GN scratch guards are also required:65 PL stack guards at init,65 at CRT check,212 tensor guards,7 metadata guards, optional2 GN scratch guards. PS CRT guards use coherent PS memory and do not increment CDMA transfers. The auditor checks counts/bytes, masks, flags, owner and full job coverage; no field is normalized to force an old count.

The old39fb432d sample remains a historical replay, with686600903 cycles, load4,031,236,701ns and resident9,270,748,114ns. New candidate cycles/time/speedup remain null. R2, strict ORT, historical-byte difference and top1/top5 are distinct. Ground truth is absent, so recognition accuracy is UNAVAILABLE.

COMBINED is an offline composition of individually tested math/control changes.2-tile is a conservative bounded choice before real register/stack/spill audit, not an observed optimum.4-tile and chunk1/4/16 remain independent. G2_TIMING is a separate diagnostic candidate and excluded from composition. No SD, PL, clock, reset or board action is part of this offline audit.
