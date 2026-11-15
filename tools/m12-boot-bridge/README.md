# M12 boot-bridge memory contracts — recovered source subset

This directory holds a selected, byte-identical MIT-licensed subset from the M12 research branch: a bounded FDT memory contract, host bridge logic and a host test of dynamic reservations.

The broader original M12 boot bridge, ADT builder, image loader, QEMU harness and T3 diagnostics have **not** been promoted with this subset. This is not a bootable Android image or a standalone physical bridge; inspect the complete original local integration tree before building a device candidate.

The source was matched to pinned local M12 Git objects, including the T3 checkpoint 59726684744df751496979ba72907eb69ceb0715. Original worktrees were not rewritten. Dynamic placement is a host/QEMU dry-run and cannot establish HLOS ownership, QHEE grants or DMA exclusion.

[Kernel/handoff report](../../docs/research/m12-kernel-and-handoff.md) · [Memory security scope](../../docs/research/qualcomm-memory-security.md)

No vendor binary, device dump, firmware artifact or unqualified privileged operation is included. The MIT SPDX declarations remain in the C sources; the README is Apache-2.0 documentation.
