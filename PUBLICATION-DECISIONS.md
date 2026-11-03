# Publication decisions — v0.1.0

Decisions taken by the project owner, Salatiel Mangabeira, on 2026-11-02. None of them was inferred by an assistant; the RC2 version of this file (commit 9dee1e5) listed them as open.

| # | Question | Decision |
|---|---|---|
| 1 | License of project-owned code and documentation | **Apache-2.0.** XNU modifications, when published, stay under APSL-2.0. |
| 2 | Authorship and redistribution of the two inherited BSP files | **Owner confirms authorship** (created in the owner's lab by an assistant working for the owner); relicensed from MIT to Apache-2.0. |
| 3 | Confidential security channel | **GitHub Private Vulnerability Reporting only.** No e-mail address is published. It must be enabled and verified on the public repository before SECURITY.md is relied upon. |
| 4 | Release authorization and residual risk | **Private first.** Publish to a private repository in the `PenangXNU` organization, obtain an independent review there, then the owner decides on public visibility. |
| 5 | Scope of v0.1 | Current preview (contracts, validator, tests, decision records, generated status) updated with P0.11 and OBS-D1. Kernel research code follows in v0.2 after its own audit. |
| 6 | Apple material | Userland work may live in the repository, but **nothing original from Apple** (BYO IPSW), per [ADR-0007](docs/adr/ADR-0007-original-experience.md) (accepted). |
| 7 | Contributions | Developer Certificate of Origin (`Signed-off-by`), no CLA. |
| 8 | Commit identity | Owner's name with the GitHub no-reply address. Existing placeholder commits are kept and mapped through `.mailmap` (no history rewrite). |
| 9 | Documentation language | English primary, Portuguese summary in [docs/pt-BR](docs/pt-BR/README.md). |

## Still open

- **Independent review** of this release in the private repository (prepared, not yet performed). An assistant's self-review is not independent approval.
- **Public visibility**: owner's explicit authorization after the review.
- **Legal review** before any tooling automates the use of Apple software on non-Apple hardware (out of scope for v0.1).

## Finalization choices (owner, 2026-11-02)

- Include the documentary P0.12 result in v0.1.0; keep the kernel worktree private.
- Accept independent local review conditionally on fixing identified issues.
- Authorize corrections, tests and local commits; no blanket remote push permission.
- Pin GitHub Actions to verified commit SHAs.
- Require pull requests, passing CI and an independent review on main, subject to actual GitHub plan capabilities.
- Require PVR to be enabled and verified at public opening; do not represent it active in the private repository.
- Permit the v0.1.0 tag only when gates are satisfied; ask for explicit final approval before making the repo public.
