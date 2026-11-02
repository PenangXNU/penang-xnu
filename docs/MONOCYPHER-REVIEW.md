# Monocypher — external research component review item

The pinned P0.10 records identify Monocypher 4.0.2. Its kernel use is verification, not signing, as documented for that research provider.
RC2 does not incorporate this library or provider, and does not change P0.10's dependency.

Monocypher 4.0.3 was released on 15 June 2026 with a correction for signature-timing behavior in EdDSA/Ed25519. Primary sources, consulted 2026-11-02: [upstream changelog](https://monocypher.org/changelog) and [upstream disclosure](https://monocypher.org/quality-assurance/disclosures).

The effect on the incorporated P0.10 code remains INDEPENDENT_TECHNICAL_REVIEW_REQUIRED.
Verification-only use does not, by itself, settle the impact of shared code, compiled paths or the complete threat model.
This preview neither declares the P0.10 implementation vulnerable nor declares it audited or unaffected. An earlier audit of an upstream version is not approval of the incorporated code.

That technical impact review is outside RC2's documentary/public-source review. No source/library download or update is performed here.
