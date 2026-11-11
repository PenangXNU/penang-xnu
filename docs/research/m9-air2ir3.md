# M9 — AIR2IR3 recovery

**Source:** original research/m9-air-nir-frontend at 2c21032a0f61867880dff055a884a2e492c7f130. **Target:** Adreno 619, chip ID 0x06010900, ir3 generation 6. **Mesa reference:** 5f31557bb1a51004a8bbac397af99fd6a2cbb59a.

The [published source](../../tools/air2ir3/README.md) retains the 41 Git-tracked files from the historical implementation. No proprietary input, compiled Mesa, metallib or GPU dump is present.

| M9 stage | Recorded result | Exact scope |
| --- | --- | --- |
| A — CLI | CLI validation 61/61 sanitised and 60/60 full; JSON and explicit negative error codes | HOST_ONLY |
| B — O1 structural | 180/180 metallib entries reconciled against LLVM 18 metadata in a private corpus | HOST_ONLY; private corpus not published |
| C — AIR→NIR→ir3 | MVP-AIR-0 compiled 10 eligible inputs; 42/42 sanitised compile tests; deterministic ir3 output and assembler round-trip | HOST_ONLY, no GPU execution |
| D — O2/O3 | O2 compared 294 floating components (286 bit-exact and eight NaN payload cases); O3 33/33 checks | HOST_ONLY |
| ARM64 | Freestanding frontend built as Mach-O ARM64 objects/archive | BUILDS only |

The census distinguished 41 vertex and 119 fragment TYPE 0/1 entries, plus 18 kernel and two additional special entries. It did not establish universal shader support. The original corpus included three metallibs and a FAT container; these inputs remain private.

**Known unsupported/incomplete work:** f16/half I/O, several air.* calls, control-flow constructs, multiple defined functions, fragment air.position inputs, named varying linkage and full runtime resource binding. Two explicit failure statuses (E_NIR and E_BACKEND) are implemented but not proven by the historical test inputs. The ir3 backend semantic equivalence is not established by assembler round-trip or decode.

**Non-claims:** No iPadOS Metal framework, original Apple compiler equivalence, XNU IOKit GPU stack, Qualcomm firmware loading, GPU ring submission or hardware output has been qualified. Hardware Adreno/Mesa compilation belongs to a separate driver workstream.

Local evidence was recorded in the M9 A–D checkpoint dated 2026-10-29. Its private result files are not included in this publication. For new work, extend the current MVP-AIR-0 and its synthetic tests rather than creating another parser.
