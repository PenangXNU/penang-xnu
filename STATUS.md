# Status — 2026-11-02

This preview packages the accepted W1 foundation and host-qualified W2/W3 components. Current public test results are in W4-PUBLICATION-GATES.json. Preserved W1 project-status.json is a historical documentary snapshot, not a declaration that its old pending work is still current.

P0.9 cryptography is integrated in a QEMU_VIRT DEV kernel according to the pinned gates/handoff at 085735f429c795575be3f460256f3edd0367c798. This preview read the documentary records only, imported no crypto implementation or binary, and performed no QEMU execution. Provenance: evidence/research-status.json.

B3 remains BLOCKED. D1–D4 are NOT_DEMONSTRATED. No new EL1, physical XNU/device, GPU or userland observation exists here. Sofiar remains P1/UNKNOWN. Apple binary preservation and accepted ADR-0004/0006 remain in force. No general arm64e/PAC incompatibility claim is reopened.

W2/W3 historical engineering validation closed G4 with local unifdef2.12. That dependency and the XNU-source-dependent PMAP tests are not required by this public subset. An omitted test is not silently counted as PASS.

PUBLICATION_BLOCKED: independent review and explicit owner approval are pending. No remote, push, visibility change or CI execution is authorized by this candidate.
