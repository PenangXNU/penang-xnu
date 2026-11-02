# Initial issues — v0.1

Candidates for the first public issues. None needs a phone or Apple files unless stated. Earlier W4/RC2 drafts remain in Git history.

| ID | Title | Acceptance evidence |
|---|---|---|
| V01-001 | Independent review of v0.1 | Content, history, licenses, claims and reproducibility reviewed by someone other than the preparing assistant; findings recorded. |
| V01-002 | Reproduce the public suite on a clean Linux host | Only public dependencies; tool versions, cases and limits recorded. |
| V01-003 | Independently review the C ABI and the pre-MMU guard | Calling convention, clobbers and fail-stop behaviour confirmed; compile success is not EL1 execution. |
| V01-004 | Review the Monocypher incorporated-code impact | Verification-only use in P0.10 confirmed or refuted against the 4.0.3 disclosure. |
| V01-005 | Design note: GICv3 IOInterruptController for B5 | Interface to `PassthruInterruptController` and IOKit interrupt APIs, from public XNU sources; no implementation claim. |
| V01-006 | Design note: minimal IOPMGR for single-CPU QEMU | Contract derived from `AppleARMSMP.cpp` consumers; no implementation claim. |
| V01-007 | Contract tests for holi vs trinket descriptors | Shared contract proven on both descriptors with negative cases; no hardware claim. |
| V01-008 | Plan OBS-2 (own payload on the G53) | Non-destructive procedure, recovery path, what it measures (TGRAN16, watchdog, ramoops); requires owner approval to run. |
| V01-009 | ADR draft: trust policy for user-supplied Apple binaries | Options, security consequences, legal questions; no implementation. |

Drafts do not authorize device operations or publication by themselves.
