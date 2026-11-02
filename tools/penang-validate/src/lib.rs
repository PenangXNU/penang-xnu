// SPDX-License-Identifier: Apache-2.0
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{
    collections::BTreeMap,
    fs,
    path::{Component, Path},
};

const REQUIRED: [&str; 8] = [
    "cpu_arm64",
    "granule_16k",
    "memory_map",
    "interrupt_controller",
    "arch_timer",
    "psci",
    "console",
    "initialization",
];

#[derive(Deserialize, Serialize, Clone)]
#[serde(deny_unknown_fields)]
pub struct Capability {
    pub state: String,
    pub origin: String,
    pub evidence_id: String,
    pub scope: String,
    pub enabled: bool,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Profile {
    pub schema_version: String,
    pub platform: String,
    pub backend: String,
    pub identity_status: String,
    pub subject: String,
    pub xnu_source_commit: String,
    pub target_contract_ref: String,
    pub required: Vec<String>,
    pub capabilities: BTreeMap<String, Capability>,
    pub resources: Resources,
}
#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Resources {
    cpu_identity: String,
    translation: TranslationResource,
    memory: MemoryResource,
    interrupts: InterruptResource,
    timer: TimerResource,
    psci: PsciResource,
    console: ConsoleResource,
    initialization: InitializationResource,
}
#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct TranslationResource {
    granule_required: u32,
    pa_bits_required: u32,
    va_bits_required: u32,
    measurement: String,
}
#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct MemoryResource {
    source: String,
    ranges: Option<Value>,
}
#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct InterruptResource {
    kind: String,
    xnu_operational: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    offline_descriptor: Option<Value>,
}
#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct TimerResource {
    register: String,
    frequency_hz: Option<u64>,
}
#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct PsciResource {
    method: String,
    runtime_verified: bool,
}
#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct ConsoleResource {
    kind: String,
    runtime_verified_in_this_work_order: bool,
}
#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct InitializationResource {
    historical: String,
    current: String,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct EvidenceSource {
    path: String,
    sha256: String,
    origin: String,
    platforms: Vec<String>,
    scope: String,
    claims: BTreeMap<String, Vec<String>>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct SourceLock {
    version: String,
    purpose: String,
    review_required_for_changes: bool,
    files: BTreeMap<String, String>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct EvidenceRegistry {
    schema_version: String,
    sources: BTreeMap<String, EvidenceSource>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SnapshotInput {
    pub valid: u32,
    pub current_el: u64,
    pub midr_el1: u64,
    pub id_aa64mmfr0_el1: u64,
    pub sctlr_el1: u64,
    pub observed_tcr_el1: u64,
    pub cntfrq_el0: u64,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Observation {
    pub schema_version: String,
    pub platform: String,
    pub subject: String,
    pub environment: String,
    pub snapshot: SnapshotInput,
    pub desired_tcr_el1: u64,
    pub memory: Vec<RegionInput>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RegionInput {
    pub base: u64,
    pub size: u64,
    pub kind: u32,
}
#[repr(C)]
pub struct Snapshot {
    pub valid: u32,
    pub subject: u32,
    pub current_el: u64,
    pub midr_el1: u64,
    pub id_aa64mmfr0_el1: u64,
    pub sctlr_el1: u64,
    pub observed_tcr_el1: u64,
    pub cntfrq_el0: u64,
}
#[repr(C)]
pub struct Request {
    pub abi_version: u32,
    pub granule: u32,
    pub pa_bits: u32,
    pub va_bits: u32,
}
#[repr(C)]
pub struct Region {
    pub base: u64,
    pub size: u64,
    pub kind: u32,
    pub reserved: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct NativeResult {
    pub status: u32,
    pub code: u32,
    pub pa_bits: u32,
    pub granule_state: u32,
}
extern "C" {
    fn pv_qemu_validate(s: *const Snapshot, tcr: u64, q: *const Request) -> NativeResult;
    fn pv_validate_memory(regions: *const Region, count: usize, q: *const Request) -> NativeResult;
    #[cfg(test)]
    fn pv_qemu_read_el1(s: *mut Snapshot) -> u32;
    fn pv_holi_validate_inventory() -> i32;
    fn pv_trinket_require_runtime() -> u32;
}
pub fn request() -> Request {
    Request {
        abi_version: 0x10000,
        granule: 16384,
        pa_bits: 40,
        va_bits: 47,
    }
}
pub fn translation(s: &Snapshot, desired: u64) -> NativeResult {
    // C only borrows s/request for this call and retains no pointer.
    unsafe { pv_qemu_validate(s, desired, &request()) }
}
pub fn memory(regions: &[Region]) -> NativeResult {
    unsafe { pv_validate_memory(regions.as_ptr(), regions.len(), &request()) }
}

fn state(status: u32) -> &'static str {
    match status {
        0 => "PASS",
        1 => "FAIL",
        2 => "BLOCKED",
        _ => "NOT_RUN",
    }
}
fn code(c: u32) -> &'static str {
    match c {
        0 => "OK",
        1 => "NULL_ARGUMENT",
        2 => "CONTRACT_ABI",
        3 => "GRANULE_UNSUPPORTED",
        4 => "FEATURE_UNKNOWN",
        5 => "TCR_GRANULE",
        6 => "PA_RANGE",
        7 => "TCR_VA",
        8 => "TCR_MODE",
        9 => "EXCEPTION_LEVEL",
        10 => "MMU_STATE",
        11 => "CPU_IDENTITY",
        12 => "MEMORY_OVERFLOW",
        13 => "MEMORY_CONTAINMENT",
        14 => "MEMORY_OVERLAP",
        15 => "MEMORY_ALIGNMENT",
        16 => "MEMORY_REQUIRED",
        17 => "TCR_CACHE",
        18 => "ASID_CAPACITY",
        19 => "PLATFORM_SUBJECT",
        _ => "NATIVE_STATUS_UNKNOWN",
    }
}
fn local(root: &Path, name: &str) -> Result<std::path::PathBuf, String> {
    let relative = Path::new(name);
    if relative.components().any(|p| {
        matches!(
            p,
            Component::ParentDir | Component::RootDir | Component::Prefix(_)
        )
    }) {
        return Err("path must remain relative to the selected repository".into());
    }
    let p = root
        .join(relative)
        .canonicalize()
        .map_err(|_| format!("missing local file: {name}"))?;
    let r = root
        .canonicalize()
        .map_err(|_| "missing repository root".to_string())?;
    if !p.starts_with(r) {
        return Err("local reference escapes repository".into());
    }
    Ok(p)
}
fn read<T: for<'a> Deserialize<'a>>(p: &Path) -> Result<T, String> {
    let bytes = fs::read(p).map_err(|_| "input file not readable".to_string())?;
    serde_json::from_slice(&bytes).map_err(|e| format!("invalid input schema: {e}"))
}
fn check(checks: &mut Vec<Value>, status: &str, code: &str, message: &str) {
    checks.push(json!({"status":status,"code":code,"message":message}));
}
pub fn evaluate(
    root: &Path,
    platform: &str,
    profile_name: &str,
    observation: Option<&Path>,
    contract_only: bool,
) -> Value {
    let mut checks = Vec::new();
    let profile: Profile = match local(root, profile_name).and_then(|p| read(&p)) {
        Ok(p) => p,
        Err(e) => {
            return json!({"schema_version":"0.1","status":"FAIL","checks":[{"status":"FAIL","code":"PROFILE_SCHEMA","message":e}],"runtime_status":"NOT_RUN","runtime_enabled":false})
        }
    };
    let expected = match platform {
        "qemu-virt" => ("qemu-virt-v1", "DECLARED_VIRTUAL", "VIRTUAL_CPU"),
        "qualcomm-holi" => ("qualcomm-holi-v1", "INVENTORIED_ANDROID", "PHYSICAL_CPU"),
        "qualcomm-trinket" => ("qualcomm-trinket-candidate-v1", "UNKNOWN", "PHYSICAL_CPU"),
        _ => ("INVALID", "UNKNOWN", "UNKNOWN"),
    };
    if profile.platform != platform || profile.backend != expected.0 {
        check(
            &mut checks,
            "FAIL",
            "BACKEND_MISMATCH",
            "platform identity and compiled backend differ",
        );
    }
    if profile.identity_status != expected.1 || profile.subject != expected.2 {
        check(
            &mut checks,
            "FAIL",
            "IDENTITY_PROVENANCE",
            "identity exceeds the admitted platform scope",
        );
    }
    if profile.schema_version != "1.0"
        || profile.xnu_source_commit != "43a90889846e00bfb5cf1d255cdc0a701a1e05a4"
        || profile.target_contract_ref
            != "contracts/platform/foundation/manifests/target-contract.json"
    {
        check(
            &mut checks,
            "FAIL",
            "TARGET_CONTRACT",
            "profile must consume the selected W1 target and XNU revision",
        );
    }
    let r = &profile.resources;
    if r.interrupts.xnu_operational != "UNKNOWN"
        || r.psci.runtime_verified
        || r.console.runtime_verified_in_this_work_order
        || r.initialization.current != "NOT_RUN"
        || r.translation.measurement != "UNKNOWN"
    {
        check(
            &mut checks,
            "FAIL",
            "CAPABILITY_CONTRADICTION",
            "descriptor cannot promote static/Android resources to XNU runtime",
        );
    }
    let virtual_platform = platform == "qemu-virt";
    let holi = platform == "qualcomm-holi";
    let cpu = if virtual_platform {
        "Cortex-A76 selected virtual CPU"
    } else if holi {
        "Kryo 4xx Android inventory"
    } else {
        "UNKNOWN"
    };
    let gic = if virtual_platform || holi {
        "GICv3"
    } else {
        "UNKNOWN"
    };
    let memory_source = if virtual_platform {
        "EFI allocated loader window; validated by existing loader contract"
    } else {
        "UNKNOWN"
    };
    if r.cpu_identity != cpu
        || r.interrupts.kind != gic
        || r.memory.source != memory_source
        || r.memory.ranges.is_some()
        || r.timer.frequency_hz.is_some()
        || r.timer.register
            != if virtual_platform {
                "CNTFRQ_EL0"
            } else {
                "UNKNOWN"
            }
        || r.psci.method != if holi { "smc" } else { "UNKNOWN" }
        || r.console.kind != if virtual_platform { "PL011" } else { "UNKNOWN" }
        || r.initialization.historical
            != if virtual_platform {
                "P0.6 reached B3"
            } else {
                "UNKNOWN"
            }
        || r.translation.granule_required != 16384
        || r.translation.pa_bits_required != 40
        || r.translation.va_bits_required != 47
    {
        check(
            &mut checks,
            "FAIL",
            "RESOURCE_PROVENANCE",
            "resource values differ from the admitted descriptor scope",
        );
    }
    if holi {
        let original: Result<Value, String> =
            local(root, "manifests/penang-bsp-offline.json").and_then(|p| read(&p));
        if !original
            .ok()
            .is_some_and(|v| r.interrupts.offline_descriptor.as_ref() == Some(&v["gic"]))
        {
            check(
                &mut checks,
                "FAIL",
                "RESOURCE_PROVENANCE",
                "GIC metadata differs from audited offline inventory",
            );
        }
    } else if r.interrupts.offline_descriptor.is_some() {
        check(
            &mut checks,
            "FAIL",
            "RESOURCE_PROVENANCE",
            "physical GIC inventory cannot qualify another backend",
        );
    }
    for name in REQUIRED {
        if !profile.capabilities.contains_key(name) || !profile.required.iter().any(|x| x == name) {
            check(&mut checks, "FAIL", "REQUIRED_CAPABILITY", name);
        }
    }
    let admitted: Result<SourceLock, String> =
        local(root, "contracts/platform/foundation.lock.json").and_then(|p| read(&p));
    match admitted {
        Err(_) => check(
            &mut checks,
            "FAIL",
            "EVIDENCE_INTEGRITY",
            "platform source admission lock unavailable",
        ),
        Ok(lock) => {
            if lock.version != "W1-minimum-1"
                || !lock.review_required_for_changes
                || lock.purpose.is_empty()
            {
                check(
                    &mut checks,
                    "FAIL",
                    "EVIDENCE_INTEGRITY",
                    "platform lock must reuse the W1 review contract",
                );
            }
            for name in [
                "contracts/platform/evidence.json",
                "contracts/platform/platform.schema.json",
                "contracts/platform/observation.schema.json",
            ] {
                if !lock.files.contains_key(name) {
                    check(
                        &mut checks,
                        "FAIL",
                        "EVIDENCE_INTEGRITY",
                        "mandatory source/schema not pinned",
                    );
                }
            }
            for (name, expected) in lock.files {
                let actual = local(root, &name)
                    .and_then(|p| fs::read(p).map_err(|_| "unreadable".to_string()));
                if !actual
                    .ok()
                    .is_some_and(|bytes| format!("{:x}", Sha256::digest(bytes)) == expected)
                {
                    check(&mut checks, "FAIL", "EVIDENCE_INTEGRITY", &name);
                }
            }
        }
    }
    let registry: Option<EvidenceRegistry> =
        match local(root, "contracts/platform/evidence.json").and_then(|p| read(&p)) {
            Ok(r) => Some(r),
            Err(e) => {
                check(&mut checks, "FAIL", "EVIDENCE_MISSING", &e);
                None
            }
        };
    if let Some(registry) = registry {
        if registry.schema_version != "1.0" {
            check(
                &mut checks,
                "FAIL",
                "EVIDENCE_SCHEMA",
                "unknown evidence ABI",
            );
        }
        // Verify every admitted source, including unused references, before accepting claims.
        for (id, source) in &registry.sources {
            match local(root, &source.path)
                .and_then(|p| fs::read(p).map_err(|_| "source unreadable".to_string()))
            {
                Ok(bytes) => {
                    if format!("{:x}", Sha256::digest(bytes)) != source.sha256 {
                        check(&mut checks, "FAIL", "EVIDENCE_INTEGRITY", id);
                    }
                }
                Err(_) => check(&mut checks, "FAIL", "EVIDENCE_MISSING", id),
            }
        }
        for (name, cap) in &profile.capabilities {
            if !["SUPPORTED", "UNSUPPORTED", "UNKNOWN"].contains(&cap.state.as_str()) {
                check(&mut checks, "FAIL", "CAPABILITY_STATE", name);
            }
            if cap.enabled && cap.state != "SUPPORTED" {
                check(&mut checks, "FAIL", "CAPABILITY_CONTRADICTION", name);
            } else if cap.enabled {
                check(
                    &mut checks,
                    "FAIL",
                    "CAPABILITY_UNQUALIFIED",
                    "this increment admits no runtime-enabled capability",
                );
            }
            match registry.sources.get(&cap.evidence_id) {
                None => check(&mut checks, "FAIL", "EVIDENCE_MISSING", name),
                Some(source) => {
                    if cap.origin != source.origin
                        || !source.platforms.iter().any(|x| x == platform)
                        || cap.scope != source.scope
                    {
                        check(&mut checks, "FAIL", "ORIGIN_MISMATCH", name);
                    }
                    if !source
                        .claims
                        .get(name)
                        .is_some_and(|states| states.contains(&cap.state))
                    {
                        check(&mut checks, "FAIL", "CAPABILITY_UNQUALIFIED", name);
                    }
                }
            }
        }
    }
    let mut translation_result = json!({"status":"NOT_RUN"});
    let mut memory_result = json!({"status":"NOT_RUN"});
    let mut legacy = "NOT_RUN";
    if platform == "qualcomm-holi" {
        if unsafe { pv_holi_validate_inventory() } == 0 {
            legacy = "PASS_HOST_METADATA";
        } else {
            check(
                &mut checks,
                "FAIL",
                "LEGACY_BSP",
                "existing Penang metadata validator rejected inventory",
            );
        }
    }
    if platform == "qualcomm-trinket" && unsafe { pv_trinket_require_runtime() } != 2 {
        check(
            &mut checks,
            "FAIL",
            "CANDIDATE_BACKEND",
            "Trinket cannot advertise runtime readiness",
        );
    }
    if let Some(path) = observation {
        match read::<Observation>(path) {
            Err(e) => check(&mut checks, "FAIL", "OBSERVATION_SCHEMA", &e),
            Ok(o) => {
                if o.environment != "HOST_SYNTHETIC" || o.schema_version != "1.0" || !contract_only
                {
                    check(
                        &mut checks,
                        "FAIL",
                        "UNADMITTED_OBSERVATION",
                        "public fixtures are host-only; no new runtime receipt is admitted",
                    );
                } else if platform != "qemu-virt"
                    || o.platform != platform
                    || o.subject != "VIRTUAL_CPU"
                {
                    check(
                        &mut checks,
                        "FAIL",
                        "PLATFORM_SUBJECT",
                        "virtual register sample cannot qualify a physical platform",
                    );
                } else {
                    let s = Snapshot {
                        valid: o.snapshot.valid,
                        subject: 1,
                        current_el: o.snapshot.current_el,
                        midr_el1: o.snapshot.midr_el1,
                        id_aa64mmfr0_el1: o.snapshot.id_aa64mmfr0_el1,
                        sctlr_el1: o.snapshot.sctlr_el1,
                        observed_tcr_el1: o.snapshot.observed_tcr_el1,
                        cntfrq_el0: o.snapshot.cntfrq_el0,
                    };
                    let t = translation(&s, o.desired_tcr_el1);
                    translation_result = json!({"status":state(t.status),"code":code(t.code),"cpu_pa_bits":t.pa_bits,
                        "qualification":"HOST_SYNTHETIC_ONLY"});
                    check(
                        &mut checks,
                        state(t.status),
                        code(t.code),
                        "shared ARM64 decoder and QEMU backend",
                    );
                    let regions: Vec<Region> = o
                        .memory
                        .iter()
                        .map(|r| Region {
                            base: r.base,
                            size: r.size,
                            kind: r.kind,
                            reserved: 0,
                        })
                        .collect();
                    let m = memory(&regions);
                    memory_result = json!({"status":state(m.status),"code":code(m.code),"qualification":"HOST_SYNTHETIC_ONLY"});
                    check(
                        &mut checks,
                        state(m.status),
                        code(m.code),
                        "shared memory/reservation validator",
                    );
                }
            }
        }
    } else {
        check(
            &mut checks,
            "BLOCKED",
            "CURRENT_OBSERVATION_MISSING",
            "no current register or usable-memory observation; runtime remains unqualified",
        );
    }
    let status = if checks.iter().any(|c| c["status"] == "FAIL") {
        "FAIL"
    } else if checks.iter().any(|c| c["status"] == "BLOCKED") {
        "BLOCKED"
    } else {
        "PASS"
    };
    json!({"schema_version":"0.1","platform":platform,"backend":profile.backend,"identity_status":profile.identity_status,
        "scope":if observation.is_some() && contract_only {"HOST_CONTRACT"} else {"DESCRIPTOR_ONLY"},
        "status":status,"checks":checks,"translation":translation_result,"memory":memory_result,
        "runtime_status":"NOT_RUN","runtime_enabled":false,"capabilities":profile.capabilities,
        "resources":profile.resources,"legacy_bsp_status":legacy,"publication_status":"PUBLICATION_BLOCKED"})
}

#[cfg(test)]
mod tests {
    use super::*;
    fn sample() -> Snapshot {
        Snapshot {
            valid: 1,
            subject: 1,
            current_el: 4,
            midr_el1: 0x410fd0b0,
            id_aa64mmfr0_el1: 0x100002,
            sctlr_el1: 0,
            observed_tcr_el1: 0,
            cntfrq_el0: 62500000,
        }
    }
    #[test]
    fn tgran16_is_positive_one_not_zero() {
        let mut s = sample();
        assert_eq!(translation(&s, 0x226511a511).status, 0);
        s.id_aa64mmfr0_el1 = 2;
        assert_eq!(translation(&s, 0x226511a511).code, 3);
    }
    #[test]
    fn future_granule_encoding_is_unknown_not_supported() {
        let mut s = sample();
        s.id_aa64mmfr0_el1 = 0xf00002;
        assert_eq!(translation(&s, 0x226511a511).status, 2);
    }
    #[test]
    fn sixteen_bit_asids_need_feature_support() {
        let mut s = sample();
        assert_eq!(translation(&s, 0x326511a511).code, 18);
        s.id_aa64mmfr0_el1 |= 2 << 4;
        assert_eq!(translation(&s, 0x326511a511).status, 0);
    }
    #[test]
    fn region_end_is_exclusive_and_adjacent_is_valid() {
        let r = [
            Region {
                base: 0x40000000,
                size: 0x100000,
                kind: 1,
                reserved: 0,
            },
            Region {
                base: 0x40000000,
                size: 0x4000,
                kind: 2,
                reserved: 0,
            },
            Region {
                base: 0x40004000,
                size: 0x4000,
                kind: 3,
                reserved: 0,
            },
        ];
        assert_eq!(memory(&r).status, 0);
    }
    #[test]
    fn zero_validity_never_qualifies_registers() {
        let mut s = sample();
        s.valid = 0;
        assert_eq!(translation(&s, 0x226511a511).status, 2);
    }
    #[test]
    #[cfg(target_arch = "x86_64")]
    fn host_has_no_fabricated_el1_register_probe() {
        let mut s = sample();
        let status = unsafe { pv_qemu_read_el1(&mut s) };
        assert_eq!(status, 3);
        assert_eq!(s.valid, 0);
        assert_eq!(s.midr_el1, 0);
    }
}
