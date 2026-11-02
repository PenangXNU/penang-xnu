# Contributing

Start with an issue from [the initial backlog](docs/INITIAL-ISSUES.md). Keep changes small and cite the source commit, toolchain, command and proof scope for each technical claim.

Run python3 -B scripts/validate_public.py with the documented dependencies. Include results and negative cases in a proposed change. The suite is public-only: do not add a private file dependency to make it pass.

Use STATIC_SPECIFICATION, HOST_OBSERVATION, QEMU_OBSERVATION and DEVICE_OBSERVATION accurately. A host compile is not a boot, and Android inventory is not an XNU driver test. Unknown capabilities must not become operational by editing JSON. Locks detect inconsistent edits; they are not signatures.

Keep Apple binaries immutable. Do not submit ROMs, IPSW/KDK/firmware, dumps, raw device logs, credentials or internal agent configuration. Only submit code and data you are entitled to contribute, with the original license and notices. Do not relicense third-party code. Proposed project-owned contributions use MIT, subject to the owner's final release-license adoption; identify exceptions before submission.

Independent review of ABI, runtime/security consequences and regressions is required before integration. Public preview review does not authorize merge, hardware activity, CI or release publication. There is no automatic right to execute phone operations.
