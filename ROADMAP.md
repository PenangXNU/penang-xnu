# Roadmap

1. Review this source preview: file selection, authorship/license proposal, negative tests, claims and fresh-export reproduction.
2. Improve public portability and reproducible dependency acquisition without bundling restricted inputs.
3. Independently review ABI and pre-MMU guard; qualify full XNU build and EL1 behavior in a separate work order.
4. Implement and qualify a complete independent Image4/AMFI/TrustCache policy/provider before moving beyond B3. Crypto integration alone does not solve it.
5. Measure unresolved Penang entry privilege, granule and memory-map facts before proposing any explicitly authorized hardware operation.
6. Qualify drivers, original userland and Adreno-backed graphics against ADR-0006 D1–D4.
7. Qualify Sofiar independently as P1; a descriptor does not establish physical support.

No date or delivery promise is attached. Publication of this host-tools preview does not depend on resolving B3. Each engineering/runtime step requires its own scope, review and evidence.
