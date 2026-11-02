# penang-validate v0.1

Build through scripts/validate_public.py or cargo build --offline --locked --manifest-path tools/penang-validate/Cargo.toml with CARGO_TARGET_DIR set to .build/w2w3/cargo.

The Python entry point is validate.py. Select --platform qemu-virt, qualcomm-holi or qualcomm-trinket. Default descriptors remain BLOCKED because this increment enables no runtime capability.

For a synthetic positive contract check, supply --observation examples/qemu-host-synthetic.json --contract-only. The result must keep runtime_enabled=false and runtime_status=NOT_RUN. Do not run the privileged register backend on the host.

Return codes: 0 PASS, 1 FAIL, 2 BLOCKED, 3 NOT_RUN. The frontend returns JSON including the original proof scope. Editing evidence/profile JSON is not a route to runtime qualification; locks are consistency checks, not signatures.

See docs/VALIDATION.md at the repository root for the public subset and excluded historical tests.
