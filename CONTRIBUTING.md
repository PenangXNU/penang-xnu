# Contributing

Thanks for helping. Most useful work needs neither a phone nor any Apple file: contracts, validators, tests, QEMU tooling, documentation and reviews. Start with [the initial issues](docs/INITIAL-ISSUES.md) or the [roadmap](ROADMAP.md).

## Ground rules

1. **Nothing from Apple, Qualcomm or Motorola.** No IPSW, kernelcache, dyld shared cache, KDK, firmware, ROM, partition image, device dump, raw device log or extracted binary — not in commits, not in issues, not in PR attachments. Tools take local paths supplied by the user (BYO IPSW, [ADR-0007](docs/adr/ADR-0007-original-experience.md)).
2. **Apple binaries stay unmodified.** Adaptation belongs below the OS (loader, platform contracts, XNU platform layer, documented XNU patches).
3. **Evidence classes are exact.** Use STATIC_SPECIFICATION, HOST_OBSERVATION, QEMU_OBSERVATION and DEVICE_OBSERVATION as defined in [docs/EVIDENCE-ADMISSION.md](docs/EVIDENCE-ADMISSION.md). A host compile is not a boot; QEMU is not hardware; Android inventory is not an XNU driver test. Unknown capabilities must not become operational by editing JSON.
4. **Fail closed.** No change may weaken a signature check, trust policy, memory protection or lockdown to get further in a boot. Research mechanisms are never described as Apple security equivalence.
5. **No phone operations by default.** Nothing in this repository authorizes flashing, erasing or booting a device. Device procedures need their own documented, non-destructive plan.
6. **Generated files are not edited by hand** (STATUS.md, the README status block, manifests/current-status.json). Change the pinned projection and regenerate with `scripts/current_status.py --write`.

## Workflow

- One issue per change; larger work uses the [work-order template](docs/work-orders/WORK-ORDER-TEMPLATE.md) and ends with a [handoff](docs/work-orders/HANDOFF-TEMPLATE.md).
- Keep changes small. Cite source commits, toolchain, commands and the proof scope of every technical claim, including negative cases.
- Run `python3 -B scripts/validate_public.py` before opening a pull request; CI runs the same suite. The suite is public-only: never add a private input to make it pass.
- New files must be added to `audit/public-files.json` and `audit/file-licenses.json` (the publication scan rejects unlisted files).

## License and sign-off (DCO)

Contributions are accepted under the repository license (Apache-2.0) — modifications of XNU sources under APSL-2.0 — and must carry a Developer Certificate of Origin sign-off: `git commit -s` adds `Signed-off-by: Your Name <email>`, certifying the [DCO 1.1](https://developercertificate.org/). Do not relicense third-party code; keep its original notices and identify it in the pull request.

## AI-assisted contributions

AI coding agents are welcome and are expected to follow [AGENTS.md](AGENTS.md). The human who signs off is responsible for the change. State in the pull request which parts were AI-assisted and how they were verified.

## Review

Independent review of ABI, runtime/security consequences and regressions is required before integration. Review does not authorize hardware activity.
