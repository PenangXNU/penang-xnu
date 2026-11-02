# Security policy

This preview is experimental host tooling and metadata validation. It makes no guarantee of Secure Boot, SEP, PPL, SPTM, TXM, AMFI or Apple trust-policy equivalence. B3 remains fail-closed. Do not use synthetic checks as authorization to boot or flash a phone.

Report non-sensitive defects with a minimal synthetic reproducer and affected commit. Do not post credentials, proprietary inputs, private evidence or exploit payloads. A confidential reporting channel has not yet been designated; establishing it is a release-review item. Do not assume a nonexistent mailbox or GitHub advisory channel is monitored.

For a sensitive finding, retain the evidence privately and request a confidential channel from the maintainer using an established contact. Do not include the sensitive details in that request. No response-time or coordinated-disclosure SLA is promised by this local candidate.

Dependency versions are pinned to the tested environment, not claimed current or free of vulnerabilities. The local scanner checks known file, path and credential patterns across the proposed tree and Git objects; it is not proof that every possible secret is absent.
