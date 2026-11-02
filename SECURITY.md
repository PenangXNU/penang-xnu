# Security policy

## Reporting a vulnerability

Report sensitive findings privately through GitHub: open the repository's **Security** tab and choose **Report a vulnerability** (GitHub Private Vulnerability Reporting). This is the only confidential channel; no e-mail address is published. Do not open a public issue for credentials, private data exposure or an exploitable flaw.

Please include the affected commit, a minimal synthetic reproducer and the scope (host tooling, QEMU research kernel, device procedure). Do not send proprietary inputs, device dumps or credentials. This is a volunteer research project: reports are handled on a best-effort basis and no response-time or disclosure SLA is promised.

Non-sensitive defects can be reported as normal issues.

## What the project does and does not claim

This repository is experimental host tooling, contracts and documentation. It makes no claim of Secure Boot, SEP, PPL, SPTM, TXM, AMFI or Apple trust-policy equivalence. Do not use synthetic checks as authorization to boot or flash a phone.

The research kernel records (P0.10, P0.11) use a project research trust root separate from Apple. B3_RESEARCH_PROVIDER_RUNTIME_VERIFIED and B4_RESEARCH_PLATFORM_EXPERT_RUNTIME_VERIFIED are not APPLE_IPADOS_SECURITY_COMPATIBLE; see [STATUS.md](STATUS.md).

P0.10's Monocypher version and the upstream signing-timing disclosure are documented in [the component review note](docs/MONOCYPHER-REVIEW.md). This release imports no provider or library code.

Dependencies are pinned to the tested environment, not claimed current or vulnerability-free. Publication checks cover the explicit file inventory, known credential/key/path patterns and every local Git object; they are bounded controls, not proof of the absence of secrets, a runtime security audit or an independent approval.
