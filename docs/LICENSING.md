# Component and license review

| Material | License | Basis |
|---|---|---|
| Project-owned code and documentation (including the two Penang BSP validation files) | Apache-2.0 | Owner decision, 2026-11-02 ([PUBLICATION-DECISIONS.md](../PUBLICATION-DECISIONS.md)) |
| Modifications of Apple XNU sources (from v0.2) | APSL-2.0 | Required by the XNU license for Modifications; not part of v0.1 |
| Third-party build/test dependencies (Rust crates, Python packages) | Their own terms | Not vendored; notices in `third-party/licenses/`, ledger in [audit/dependencies.json](../audit/dependencies.json) |
| Apple, Qualcomm, Motorola software and firmware | Not included | Bring your own IPSW ([ADR-0007](adr/ADR-0007-original-experience.md)) |

audit/selected-sources.json records every selected input, its original digest, revision/source group and any adaptation. audit/file-licenses.json classifies every public file. AUTHORS.md records authorship and the historical author-field limitation.

No third-party implementation is copied into this source release. The selected W2/W3 C/Rust/Python code is project code; hardware descriptions are factual projections. XNU patches and XNU-source integration helpers are excluded from v0.1 rather than assigned a license they cannot carry.

The Cargo lock retains registry checksums. The unicode-ident license expression includes Unicode-3.0 in addition to MIT/Apache alternatives. pyrsistent includes a BSD-3-Clause toolz-derived portion; the installed distribution copyright record preserves that qualification. Build tools and standard libraries are external prerequisites, not bundled artifacts; the pins do not assert that dependencies are current, secure or suitable for a binary release.

A linked-binary release, any use of Apple inputs or operational firmware would need its own license, notice and security review.

## Post-v0.1 AIR2IR3 research exception

The recovered M9 source files under [tools/air2ir3](../tools/air2ir3/README.md) retain their original MIT SPDX declarations and the [MIT license text](../tools/air2ir3/LICENSE-MIT). They are explicitly outside the v0.1.0 Apache-2.0-only selected source set. No proprietary Apple/Mesa/Qualcomm/Motorola binary is vendored. Other newly authored research summaries in this reconciliation branch follow Apache-2.0. A future licensing change to that historical source requires a separate owner review; this publication preserves original source hashes.

## M12 isolated research code

Nine M12 C source/header/test files under [tools/m12-boot-bridge](../tools/m12-boot-bridge/README.md) and [tools/m12-penang-physical](../tools/m12-penang-physical/README.md) likewise retain their historical MIT SPDX declarations. The import is an explicitly incomplete, host/QEMU-only research subset, not a physical XNU kernel, vendor firmware, Qualcomm monitor or licensed copy of Apple private SDK code. Newly authored READMEs are Apache-2.0 documentation. The public audit registers each file independently.
