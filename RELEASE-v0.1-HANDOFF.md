# Release v0.1.0 handoff

Date: 2026-11-02. Branch: `release/v0.1`, based on W4-RC2 (`9dee1e57`).
Gates: [`RELEASE-v0.1-GATES.json`](RELEASE-v0.1-GATES.json).

## What changed since RC2

- **Status.** The derived status now covers P0.10 (QEMU first entry, crypto
  and security provider), P0.11 (QEMU platform expert) and the OBS-D1 device
  observation. All 14 sources are pinned by commit and SHA-256. Nothing is
  promoted beyond its evidence; the next blocker is
  `B5_PLATFORM_INTERRUPT_CONTROLLER`.
- **License.** Project code and documentation are Apache-2.0 (owner decision).
  XNU modifications stay APSL-2.0. The pinned W1 validator keeps its RC2 bytes.
- **Principle.** [ADR-0007](docs/adr/ADR-0007-original-experience.md):
  the goal is an experience as close as possible to the original Apple one,
  in the spirit of OpenCore, with nothing from Apple in the repository
  (bring your own IPSW).
- **Collaboration.** `AGENTS.md`, `CLAUDE.md`, work-order and handoff
  templates, issue and PR templates, `CODEOWNERS`, a validation workflow,
  DCO, a security policy using GitHub Private Vulnerability Reporting,
  a pt-BR summary and `.mailmap`.

## Validation (local, HOST scope)

`python3 scripts/validate_public.py`: all seven groups PASS
(documentary, build, rust, synthetic 36, public-build-and-boundary 27,
clippy, scan including Git history). No runtime claim: nothing here was run
on a device.

## Open items

1. Existing private GitHub Actions run was reported successful on cb71f98; the changed release candidate still requires its own CI validation.
2. Independent local review completed; corrected release candidate requires final follow-up and remote verification.
3. Owner decision on public visibility; enable Private Vulnerability
   Reporting and branch rulesets at the same time.
4. v0.2: runtime code (P0.11 platform expert and later work orders).

## Next concrete operation

Continue the isolated release candidate locally, validate, then request explicit authorization for any remote push or public visibility. No public release tag until all relevant gates pass.

## Finalization candidate (not yet published)

P0.12 GICv3/SGI1 and bsd_init-entry research evidence added as a pinned documentary reference only. B6 IOPMGR and B7 pthread provider are separate blockers. GitHub Actions dependencies are SHA-pinned; SECURITY.md correctly states the PVR channel is not operational until enabled and verified. No kernel, raw trace or device image was imported. Current public visibility requires a separate explicit owner OK.
