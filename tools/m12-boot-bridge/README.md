# M12 boot-bridge memory contracts — recovered source subset

This directory holds a selected, byte-identical MIT-licensed subset from the M12 research branch: a bounded FDT memory contract, host bridge logic and a host test of dynamic reservations.

The broader original M12 boot bridge, ADT builder, image loader, QEMU harness and T3 diagnostics have **not** been promoted with this subset. This is not a bootable Android image or a standalone physical bridge; inspect the complete original local integration tree before building a device candidate.

The source was matched to pinned local M12 Git objects, including the T3 checkpoint 59726684744df751496979ba72907eb69ceb0715. Original worktrees were not rewritten. Dynamic placement is a host/QEMU dry-run and cannot establish HLOS ownership, QHEE grants or DMA exclusion.

[Kernel/handoff report](../../docs/research/m12-kernel-and-handoff.md) · [Memory security scope](../../docs/research/qualcomm-memory-security.md)

## 2026-11-16 changes (no longer byte-identical)

`bridge_core.c` and `bridge_contract.h` were modified in place for the P0
FDT hardening ([record](../../docs/research/2026-11-16-p1-fdt-p0-hardening.md));
they keep their MIT declarations. Changes: the firmware FDT is copied into a
retained slot after `boot_args` and announced as `/chosen/penang,fdt-pa` and
`penang,fdt-length`; `rng-seed` fields of 64–512 bytes are accepted (64
forwarded, longer condensed with SHA-512); seed values are zeroed in the
retained copy. `m12_retained_fdt.c` (Apache-2.0, new) holds the copy, scrub and
SHA-512 code. `m12_physical_entropy_gate.h`, which `bridge_core.c` already
included, is now imported unchanged from the same research line (MIT,
SHA-256 `ec31a561c351585222ed4a4737e73fac4e9f99a60ca7231b214a17a5a6003020`,
research commit `acb364dc1822f7bf2ae84f75b982b4f7d52ee2c3`); it keeps the
physical entropy gate closed. Host tests: `tests/public/test_p1_fdt_retention.py`.

No vendor binary, device dump, firmware artifact or unqualified privileged operation is included. The MIT SPDX declarations remain in the C sources; the README is Apache-2.0 documentation.
