# Security policy

This preview is experimental host tooling and metadata validation. It makes no guarantee of Secure Boot, SEP, PPL, SPTM, TXM, AMFI or Apple trust-policy equivalence. Do not use synthetic checks as authorization to boot or flash a phone.

The fixed P0.10 documentary reference records authenticated acceptance and unauthorized-signer rejection under a separate project research root. B3_RESEARCH_PROVIDER_RUNTIME_VERIFIED is not APPLE_IPADOS_SECURITY_COMPATIBLE. The next recorded research stop is B4_IOKIT_PLATFORM_DRIVER. No valid Platform Expert, complete userland or physical XNU boot is demonstrated by that observation.

Report non-sensitive defects with a minimal synthetic reproducer and affected commit. Do not post credentials, proprietary inputs, private evidence or exploit payloads.
A confidential reporting channel has not been designated: HUMAN_DECISION_REQUIRED. No address, mailbox or GitHub advisory channel is claimed monitored.
For sensitive findings, retain evidence privately and request a confidential route through an already established maintainer contact, without including sensitive details. No response-time or disclosure SLA is promised.

P0.10's Monocypher version and upstream signing-timing disclosure are documented in [the component review note](docs/MONOCYPHER-REVIEW.md). RC2 imports no provider/library code and does not decide incorporated-code impact.

Dependencies remain pinned to the tested environment, not claimed current or vulnerability-free. Publication checks cover the explicit file inventory, known credential/key/path patterns and local Git objects. The checks include a synthetic deleted-secret history test and private-seed-file rejection.
They are bounded controls, not proof of absolute absence of secrets, a runtime security audit or an independent approval. See [pending publication decisions](PUBLICATION-DECISIONS.md).
