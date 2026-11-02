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
