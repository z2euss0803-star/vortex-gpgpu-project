# HUNet result / glyph component status

T13 result and glyph components passed Ubuntu native tests using explicitly
historical W4 run39fb432d2a3549a5abdf8644ca6c2d6e; no new inference occurred.

- Original classes SHA ce1a0e8c2abcd4c58438885d6697102a7b0379614c90ed1691e3fc7af86c807f, 8105 classes.
- Original logits SHA58a75990aad6711810065fa7a3c83e81a3eaaba13769f0395eac95dd7fa48654, 32420 bytes.
- Top5 ids379,1,2744,1010,368: 臣、乙、道、直、耳. They are model outputs, not ground-truth labels.
- Historical ORT failure remains false; byte-identical output classifies KNOWN_NUMERIC_DEVIATION. R2/ORT metrics remain in the original fixture receipts.
- Stable ties order by class id; nonfinite logits, wrong8105 count/class table and invalid bbox rejected.
- NotoSansSC-VF installed font SHA763146584cf0710223441356b4395e279021b0806c196614377a7a0174ae074a. Embedded SIL OFL notice recorded in config/demo/hunet-display-assets.json. All8105 codepoints present; missing glyph fallback is ASCII class id, never a substitute character.
- Binary table DMLAB1 v1: header96 bytes;8105 records of32 bytes; each atlas cell32x32 grayscale8. Class/font identities are separate from the atlas SHA.

Remaining T13 integration: borrowed owner-fd wrapper, typed fused/hybrid
execution adapter and its lifetime/error tests. T16 still must coordinate
software profiles without reopening the GPU. No HUNet runtime adapter is
registered by this component-only commit, no S8 HUNet compatibility PASS.
