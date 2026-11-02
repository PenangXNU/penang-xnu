# Component and license review

This candidate proposes MIT for project-owned code and documentation. Existing MIT file declarations are preserved. The owner must adopt or revise the proposal before publication. The review establishes recorded source/notice provenance; Git labels are not proof of a contributor's legal identity or title.

audit/selected-sources.json records every selected input, its original digest, revision/source group and any adaptation. audit/file-licenses.json classifies every proposed public file. AUTHORS.md records the historical BSP author-field limitation rather than inventing a contributor identity.

No third-party implementation is copied into this source preview. The selected W2/W3 C/Rust/Python code is project code; hardware descriptions are factual projections. The XNU patches and actual-XNU-source integration helpers are excluded rather than assigned the project's proposed license. Apple/Qualcomm images, SDKs and binaries are excluded.

The Cargo lock retains registry checksums. audit/dependencies.json records the exact locally inspected package versions, complete license expressions, repositories and original notice hashes. Original notices are copied selectively under third-party/licenses/ for review; no dependency sources are vendored. The unicode-ident license expression includes Unicode-3.0 in addition to MIT/Apache alternatives. pyrsistent includes a BSD-3-Clause toolz-derived portion; the installed distribution copyright record preserves that qualification.

Python requirements pin the tested runtime packages. Rust build/proc-macro dependencies are included in the ledger, including target-conditional packages. Build tools and standard libraries are external prerequisites, not bundled artifacts. The pins do not constitute an assertion that the dependencies are current, secure or suitable for a binary release.

Public source selection is distinct from a future linked-binary release, Apple input distribution or operational firmware. Those would need their own license/notice and security review.
