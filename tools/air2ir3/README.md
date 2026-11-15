# AIR2IR3 — experimental AIR to NIR to ir3 frontend

Research component restored from the original M9 worktree. This is an original MIT-licensed C frontend and host pipeline, not a Metal driver, an Apple compiler, an iPadOS runtime, or a Qualcomm device driver.

## Architecture

- A bounded, freestanding parser handles selected Apple AIR/LLVM-bitcode module structures and metallib entry metadata without copying Apple's compiler or libraries.
- The MVP-AIR-0 subset is lowered to Mesa NIR.
- The host-only Mesa backend emits ir3 instructions for Adreno a6xx (research target Adreno 619, chip ID 0x06010900).
- Explicit failure codes reject unsupported IR, interfaces and backend cases. No silent fallback converts unsupported shaders.
- The frontend can be cross-compiled to ARM64 Mach-O objects, but the complete NIR/ir3 host backend is not an XNU kernel component.

The independently pinned Mesa checkout used by M9 was 5f31557bb1a51004a8bbac397af99fd6a2cbb59a. Mesa is an external dependency, not vendored in this repository.

## Build and validation

- build_host.sh: sanitised freestanding-parser host CLI, without Mesa.
- build_mesa.sh and build_compile.sh: full host compilation against external Mesa build dependencies.
- build_arm64.sh: freestanding ARM64 Mach-O frontend objects and archive.
- tests/run_all.sh: host CLI, optional private-corpus structural oracle, compile tests, semantic oracle, backend oracle and ARM64 cross-build.
- tests/fixtures/synthetic/: project-authored LLVM IR tests. No original Apple metallibs are included.

The optional O1 corpus stage is skipped when its private local inputs are absent. This is not a test failure, but must not be reported as corpus verification in a clean public checkout.

## Verified scope and limits

The original M9 checkpoint records 180 structurally reconciled entries in a privately obtained corpus; 10 MVP-AIR-0 candidates passed the bounded host compilation path. It records 42/42 compile test cases under sanitizers, O2 comparisons over 294 float components, O3 backend verification in 33/33 cases and reproducible ARM64 object builds. These are historical local observations, not results of a fresh public CI run.

Half-precision I/O, several AIR intrinsics, multi-function modules, control flow, name-based varying linkage and additional shader stages remain unsupported or incomplete. No physical GPU, kernel, IOKit, Metal or device execution was qualified. E_NIR and E_BACKEND paths are implemented but were not triggered by the audited corpus.

Source provenance: local WSL research/m9-air-nir-frontend at commit 2c21032a0f61867880dff055a884a2e492c7f130. The 41 imported Git blob IDs match those originals. Historical worktree commits were deliberately not imported into public Git history.

## Legal and security boundary

Source files retain their original MIT SPDX declarations; see [LICENSE-MIT](LICENSE-MIT). Project-generated documentation follows the repository's Apache-2.0 policy. Third-party Mesa/LLVM dependencies retain their own licenses. BYO proprietary inputs: do not commit metallibs, IPSWs, firmware, GPU binaries, private traces, runtime dumps or Apple material. See [the detailed M9 checkpoint](../../docs/research/m9-air2ir3.md).
