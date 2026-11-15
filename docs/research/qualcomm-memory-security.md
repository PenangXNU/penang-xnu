# Qualcomm holi memory protection — conservative research conclusions

**Hardware:** Moto G53 XT2335-1 / SM4350-AC holi. **Research checkpoints:** B4.2, M12-B4.3A-R3, C2-P0, C2-P1 and T2/T3. These are not public firmware reproductions or hardware security certification.

- The physically measured Stage-0 handoff entered non-secure EL1. There is no proof that the payload receives independent EL2 control.
- The stock QHEE firmware contains EL2 Stage-2 management paths, but the active context and effective grants at the observed ABL handoff were not measured.
- The original Motorola kernel contains a client and transport for memory assignment via Qualcomm SCM. Static existence does not prove that an XNU NS-EL1 caller is authorized, nor that the service provides immutable RX/RO-NX lockdown.
- An ABL path contains a direct SMC after ExitBootServices and before payload handoff. Its existence does not export a general runtime SCM ABI to XNU.
- T2 implemented isolated ARM64 Stage-1 16-KiB permission transitions with TLBI and code-cache maintenance. This does not itself provide protection against compromised EL1 code, external aliases or peripheral DMA.
- FDT memory geometry and dynamic placement models do not prove ownership, QHEE grants, XPU policy or SMMU stream isolation.

## Security policy

PMAP_PHYSICAL remains BLOCKED. Do not translate Apple SPTM/PPL/AMCC requirements into no-op providers, disable checks to force native linking, assume monitor ownership at EL2 or synthesize SMC IDs. The B4.2 backend requires independently demonstrated page-granular revocation, static code/data lockdown, mediated mutation, alias control, rollback behavior and DMA enforcement.

A separately designed reduced-assurance research kernel profile would require an explicit architecture decision and visibly different guarantees. It cannot inherit the physical security gate or Apple-equivalence claims.

The detailed offsets, unmodified vendor binaries and local evidence bundles remain in the original private research area. Their absence here is intentional. Next work on this boundary requires a demonstrably supported physical backend or a new, explicitly authorised research profile, not another generic firmware scan.
