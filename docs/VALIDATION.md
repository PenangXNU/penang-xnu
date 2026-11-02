# Public validation

Run python3 -B scripts/validate_public.py from the repository root after explicitly acquiring the dependencies listed in README.md. It builds Rust/C offline and locked, runs Rust units, 36 retained synthetic CLI cases, own-source C/C++ ABI and ARM64 compile checks, publication-boundary negatives and the source/history scanner. It does not fetch or execute XNU, QEMU, firmware or device tools.

Build outputs and local logs stay in .build/. The C++ ABI consumer runs on the host. ARM64 objects and the pre-MMU guard are compiled only. The optional ELF/Mach-O headers checked by tests establish an object format, not runtime effects.

The three default profiles deliberately remain BLOCKED, exit2 and runtime_enabled=false. The supplied synthetic example can PASS only as HOST_CONTRACT, with runtime_status=NOT_RUN. Exit1 denotes FAIL; exit3 denotes NOT_RUN for missing dependency or input. Do not convert these into hardware support.

The W1 core validator is reused byte-identically by the Python frontend for target, schema, source and derivation checks. It checks documentary consistency, not independent runtime proof. The accepted ADRs are preserved byte for byte.

The source-compatibility test against actual Apple XNU headers, the old PMAP/link groups and the private-fixture legacy suite are excluded. Their historical results are described separately; they are not requirements of this public suite and do not inflate its counts.

The local scanner covers UTF-8/file-type policy, explicit file inventory, symlinks, selected private path and credential patterns, Markdown file targets, and all local Git objects/commit paths when a Git repository exists. Pattern scanning is bounded, not a security certification or dependency vulnerability audit. Independent review remains required.
