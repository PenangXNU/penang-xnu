#!/usr/bin/env python3
"""W1 documentary validator. Offline, read-only by default; never runs project code."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
sys.dont_write_bytecode = True
from urllib.parse import unquote, urlsplit

try:
    from jsonschema import Draft202012Validator
except ImportError:
    print(json.dumps({"ok": False, "errors": [{"code": "DEPENDENCY", "path": "requirements.txt",
          "message": "jsonschema is required; no automatic installation"}], "checks": {}}))
    sys.exit(2)

GATES = {"PASS", "FAIL", "BLOCKED", "PARTIAL", "UNKNOWN", "NOT_RUN"}
TAXONOMIES = {
    "gate_status": GATES,
    "environment": {"STATIC_ONLY", "HOST_ONLY", "QEMU_VERIFIED", "DEVICE_VERIFIED_ANDROID",
                    "DEVICE_VERIFIED_XNU", "DEVICE_VERIFIED_USERLAND", "DEVICE_VERIFIED_GPU"},
    "epistemic_status": {"PROVEN", "INFERRED", "OPEN"},
    "task_state": {"COMPLETED", "BLOCKED", "NOT_RUN", "REPORTED_ONLY", "IN_PROGRESS"},
}
GENERATED = ("manifests/project-status.json", "STATUS.md", "EVIDENCE-INDEX.md")
REQUIRED_DOCS = ("README.md", "PROJECT-CHARTER.md", "ARCHITECTURE.md", "ROADMAP.md",
                 "STATUS.md", "DECISIONS.md", "EVIDENCE-INDEX.md", "CONTRIBUTING.md",
                 "AGENTS.md", "PUBLICATION-READINESS.md")
INPUTS = ("manifests/target-contract.json", "manifests/evidence-index.json",
          "manifests/derivation-rules.json", "evidence/source-register.json")
SCHEMAS = ("schemas/target-contract.schema.json", "schemas/evidence-index.schema.json",
           "schemas/project-status.schema.json")


def encoded(data):
    return (json.dumps(data, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def no_duplicates(pairs):
    result = {}
    for k, v in pairs:
        if k in result:
            raise ValueError("duplicate JSON key: " + k)
        result[k] = v
    return result


class Audit:
    def __init__(self, root):
        self.root = root.resolve()
        self.errors = []
        self.documents = {}
        self.safe_paths = {}
        self.checks = {"profile": "public", "private_originals": "NOT_RUN",
                       "runtime": "NOT_RUN", "external_links": "NOT_CHECKED",
                       "proof_scope": "documentary consistency and bytes, not runtime qualification"}

    def error(self, code, path, message):
        self.errors.append({"code": code, "path": str(path), "message": str(message)[:1000]})

    def safe(self, name, code="PATH_BOUNDARY"):
        raw = str(name)
        if raw in self.safe_paths:
            return self.safe_paths[raw]
        path = PurePosixPath(raw)
        if chr(92) in raw or path.is_absolute() or ".." in path.parts or re.match(r"^[A-Za-z]:", raw):
            self.error(code, name, "relative path inside candidate required")
            return None
        p = self.root
        for segment in path.parts:
            p = p / segment
            if p.is_symlink():
                self.error(code, name, "symlinks are not permitted")
                return None
        self.safe_paths[raw] = p
        return p

    def json(self, name):
        p = self.safe(name)
        if p is None:
            return None
        try:
            return json.loads(p.read_text(encoding="utf-8"), object_pairs_hook=no_duplicates)
        except (OSError, ValueError) as e:
            self.error("JSON", name, e)
            return None

    def pointer(self, ref):
        file, sep, pointer = ref.partition("#")
        try:
            if file not in self.documents:
                p = self.safe(file, "SOURCE_REFERENCE")
                if p is None:
                    return None
                self.documents[file] = json.loads(p.read_text(encoding="utf-8"), object_pairs_hook=no_duplicates)
            data = self.documents[file]
            if sep and pointer:
                if not pointer.startswith("/"):
                    raise ValueError("JSON pointer must begin with /")
                for token in pointer[1:].split("/"):
                    token = unquote(token).replace("~1", "/").replace("~0", "~")
                    data = data[int(token)] if isinstance(data, list) else data[token]
            return data
        except (OSError, ValueError, KeyError, IndexError, TypeError) as e:
            self.error("SOURCE_REFERENCE", ref, "unresolvable local proof: " + str(e))
            return None

    def schemas(self, target, evidence):
        loaded = []
        for name in SCHEMAS:
            schema = self.json(name)
            if schema is None:
                continue
            def external(value):
                if isinstance(value, dict):
                    return any(k == "$ref" and not str(v).startswith("#") or external(v)
                               for k, v in value.items())
                if isinstance(value, list):
                    return any(external(v) for v in value)
                return False
            if external(schema):
                self.error("SCHEMA_REFERENCE", name, "external schema references forbidden; no network resolution")
                continue
            try:
                Draft202012Validator.check_schema(schema)
            except Exception as e:
                self.error("SCHEMA", name, e)
                continue
            loaded.append(schema)
        if len(loaded) != 3:
            return None
        for name, data, schema in zip(INPUTS[:2], (target, evidence), loaded[:2]):
            for e in Draft202012Validator(schema).iter_errors(data):
                self.error("SCHEMA", name + "/" + "/".join(map(str, e.absolute_path)), e.message)
        self.checks["schemas"] = "PASS" if not self.errors else "FAIL"
        return loaded[2]

    def lock(self, target, register):
        lock = self.json("foundation.lock.json")
        if not isinstance(lock, dict) or not isinstance(lock.get("files"), dict):
            self.error("SOURCE_INTEGRITY", "foundation.lock.json", "missing file hash lock")
            return
        required = {*SCHEMAS, INPUTS[2], INPUTS[3]}
        required.update(r["path"] for r in target["adr_registry"])
        if not required.issubset(lock["files"]):
            self.error("SOURCE_INTEGRITY", "foundation.lock.json", "required sources or schemas not pinned")
        for name, expected in lock["files"].items():
            p = self.safe(name)
            if p is None:
                continue
            if not p.is_file() or digest(p.read_bytes()) != expected:
                self.error("SOURCE_INTEGRITY", name, "missing or changed pinned bytes")
        for identity, source in register.get("sources", {}).items():
            if "local_copy" in source:
                p = self.safe(source["local_copy"], "SOURCE_REFERENCE")
                if p and (not p.is_file() or digest(p.read_bytes()) != source["origin"]["sha256"]):
                    self.error("SOURCE_INTEGRITY", identity, "ADR copy differs from recorded original hash")
        self.checks["hash_lock"] = "CONSISTENCY_ONLY_NOT_A_SIGNATURE"

    def authority(self, target, evidence):
        if target.get("status") != "CANDIDATE" or evidence.get("status") != "CANDIDATE":
            self.error("AUTHORITY", "status", "new canonical tree is CANDIDATE, not independently approved")
        auth = target.get("authority", {})
        if isinstance(auth, dict) and (auth.get("adrs_modified") is not False or
            auth.get("human_review") != "PENDING_FOR_NEW_CANONICAL_TREE"):
            self.error("AUTHORITY", "authority", "original ADRs unchanged; candidate human review pending")
        if evidence.get("independent_runtime_reproduction") is not False:
            self.error("EVIDENCE_ADMISSION", "independent_runtime_reproduction", "no new runtime was reproduced in this W1")
        if evidence.get("private_artifact_payloads_copied") is not False:
            self.error("PUBLICATION_BOUNDARY", "private_artifact_payloads_copied", "private payload admission forbidden")
        if target.get("publication_status") != "PUBLICATION_BLOCKED" or evidence.get("publication_status") != "PUBLICATION_BLOCKED":
            self.error("PUBLICATION_BOUNDARY", "publication_status", "publication review remains blocked")
        records = evidence.get("records", [])
        if isinstance(records, list):
            ids = [r.get("id") for r in records if isinstance(r, dict)]
            if len(ids) != len(set(ids)):
                self.error("SOURCE_REFERENCE", "records", "duplicate evidence identity")
        for r in records:
            if not isinstance(r, dict):
                continue
            for field, values in TAXONOMIES.items():
                if r.get(field) not in values:
                    self.error("TAXONOMY", str(r.get("id")) + "/" + field, "state belongs to wrong or unknown domain")

    def contract(self, target, register):
        os_target = target["os_target"]
        expected = {"os": "iPadOS", "version": "18.7.8", "build": "22H352",
                    "apple_reference": "iPad7,11", "userland_abi": "arm64",
                    "kernel_cpu_subtype": "ARM64_ALL", "xnu_public_source": "11417.140.69",
                    "xnu_source_commit": "43a90889846e00bfb5cf1d255cdc0a701a1e05a4",
                    "apple_kernelcache_revision": "11417.140.69.706.9"}
        if any(os_target.get(k) != v for k, v in expected.items()):
            self.error("TARGET_CONTRACT", "os_target", "accepted target or public/stock version distinction changed")
        primary = target["primary_target"]
        if any(primary.get(k) != v for k, v in {"codename": "penang", "soc": "SM4350-AC",
               "family": "holi", "gpu": "Adreno 619", "xnu_device_qualification": "NOT_DEMONSTRATED"}.items()):
            self.error("TARGET_CONTRACT", "primary_target", "accepted Penang target or qualification changed")
        secondary = target["secondary_target"]
        if any(secondary.get(k) != v for k, v in {"role": "P1_CROSS_PLATFORM_QUALIFICATION",
               "hardware_identity_status": "UNKNOWN", "xnu_device_qualification": "NOT_DEMONSTRATED",
               "substitutes_penang_D1_D4": False}.items()):
            self.error("TARGET_CONTRACT", "secondary_target", "P1 scope does not establish physical identity or delivery")
        inv = target["invariants"]
        for k, v in inv.items():
            expected_value = False if k in {"physical_uart_required", "new_hardware_or_runtime_actions_authorized"} else (
                {"xnu8796_role": "INTERNAL_SCAFFOLD_ONLY", "b3": "BLOCKED",
                 "security_provider_decision": "C", "apple_security_equivalence": "NOT_CLAIMED"}.get(k, True))
            if v != expected_value:
                self.error("TARGET_CONTRACT", "invariants/" + k, "accepted preservation/security invariant changed")
        adrs = {r["id"]: r for r in target["adr_registry"]}
        for identity in ("ADR-0004", "ADR-0006"):
            if identity not in adrs or adrs[identity]["authority"] != "ACCEPTED":
                self.error("ADR_CONTRACT", identity, "accepted ADR missing or authority downgraded")
        try:
            p = self.safe(adrs["ADR-0006"]["path"], "SOURCE_REFERENCE")
            lines = p.read_text(encoding="utf-8").splitlines() if p else []
            canonical = {}
            for i, line in enumerate(lines, 1):
                m = re.match(r"- D-(\d) (.*)", line)
                if m:
                    canonical["D" + m[1]] = (m[2].replace(chr(96), ""), i)
            criteria = {r["id"]: r for r in target["delivery_criteria"]}
            if set(criteria) != {"D1", "D2", "D3", "D4"} or len(target["delivery_criteria"]) != 4:
                raise ValueError("exact four delivery criteria required")
            for identity, r in criteria.items():
                if (r["requirement"], r["source_line"]) != canonical.get(identity) or r["source_id"] != "S11":
                    raise ValueError("criterion differs from accepted ADR source: " + identity)
        except (KeyError, ValueError, OSError) as e:
            self.error("ADR_CONTRACT", "delivery_criteria", e)
        intake = target["input_admission"]
        repair = register["selected_facts"]["intake"]
        if (intake["selected_revision"] != repair["version"] or
            intake["zip_sha256"] != repair["zip_sha256"] or
            intake["manifest_sha256"] != repair["manifest_sha256"] or
            intake["integrity"] != "PASS" or intake["accepted_for"] != "INPUT_BYTES_ONLY" or
            intake["semantic_approval"] != "NOT_GRANTED_BY_REPAIR" or
            intake["historical"] != {"version": "0.1", "zip_sha256":
                "873e257d2e153c9e65e60e4edf48836484d3779d11922852d2b35eb836a83471", "W1_G1": "FAIL"}):
            self.error("INPUT_INTEGRITY", "input_admission", "repaired intake proof or immutable historical failure changed")

    def provenance(self, target, evidence, rules, register):
        sources = register.get("sources", {})
        known = set(sources)
        ids = [r["id"] for r in evidence["records"]]
        if len(ids) != len(set(ids)):
            self.error("SOURCE_REFERENCE", "records", "duplicate evidence id")
        if set(ids) != set(register["admitted_records"]):
            self.error("EVIDENCE_ADMISSION", "records", "record set differs from separately pinned admission")
        for identity, ref in evidence["sources"].items():
            if identity not in known:
                self.error("SOURCE_REFERENCE", identity, "unknown source")
            elif self.pointer(ref["ref"]) != sources[identity]:
                self.error("SOURCE_REFERENCE", identity, "source reference does not resolve to selected source")
        def source_ids(ids, context):
            if not ids or any(i not in known or i not in evidence["sources"] for i in ids):
                self.error("SOURCE_REFERENCE", context, "unknown or absent source id")
        for record in evidence["records"]:
            identity = record["id"]
            source_ids(record["source_ids"], identity)
            if any(i not in evidence["anchors"] for i in record["anchor_ids"]):
                self.error("SOURCE_REFERENCE", identity, "unknown artifact anchor")
            proof = self.pointer(record["proof_ref"])
            if record != register["admitted_records"].get(identity) or record != proof:
                self.error("EVIDENCE_ADMISSION", identity, "record not supported by pinned reviewed admission; no manual promotion")
        if evidence["anchors"] != register.get("artifact_anchors"):
            self.error("SOURCE_INTEGRITY", "anchors", "artifact identity anchors changed")
        adr_ids = [r["id"] for r in target["adr_registry"]]
        if len(adr_ids) != len(set(adr_ids)):
            self.error("SOURCE_REFERENCE", "adr_registry", "duplicate ADR identity")
        for r in target["adr_registry"]:
            source_ids([r["source_id"]], r["id"])
            source = sources.get(r["source_id"], {})
            if source.get("local_copy") != r["path"]:
                self.error("SOURCE_REFERENCE", r["id"], "ADR path does not match its selected source copy")
        for identity, source in (("ADR-0004", "S01"), ("ADR-0006", "S11")):
            if not any(r["id"] == identity and r["source_id"] == source for r in target["adr_registry"]):
                self.error("ADR_CONTRACT", identity, "accepted source identity changed")
        authority = target["authority"]
        if not {"ADR-0004", "ADR-0006"}.issubset(authority["accepted_decision_ids"]):
            self.error("ADR_CONTRACT", "authority", "required accepted decisions missing")
        source_ids(authority["decision_source_ids"], "authority/decision_source_ids")
        source_ids([authority["work_order_source_id"]], "authority/work_order_source_id")
        for criterion in target["delivery_criteria"]:
            source_ids([criterion["source_id"]], criterion["id"])
        if any(i not in ids for i in target["input_admission"]["evidence_ids"]):
            self.error("SOURCE_REFERENCE", "input_admission/evidence_ids", "unknown evidence reference")
        if any(i not in ids for i in rules.get("required_blocker_evidence", []) + rules.get("publication_evidence", [])):
            self.error("SOURCE_REFERENCE", "rules", "unknown blocker or publication evidence reference")
        source_ids(target["os_target"]["source_ids"], "os_target")
        source_ids(target["secondary_target"]["source_ids"], "secondary_target")
        for r in rules.get("subsystems", []):
            if not r.get("evidence_ids") or any(i not in ids for i in r["evidence_ids"]):
                self.error("SOURCE_REFERENCE", "rules/" + r.get("id", "?"), "subsystem evidence missing")
        if len({r["id"] for r in rules["subsystems"]}) != len(rules["subsystems"]):
            self.error("SOURCE_REFERENCE", "rules", "duplicate subsystem id")

    def local(self, evidence, register, index):
        self.checks["profile"] = "local"
        self.checks["private_originals"] = "FAIL"
        if not index:
            self.error("PRIVATE_INDEX", "--private-index", "explicit private index required for local profile")
            return
        try:
            data = json.loads(Path(index).read_text(encoding="utf-8"), object_pairs_hook=no_duplicates)
            expected = {i: s["origin"]["sha256"] for i, s in register["sources"].items()}
            expected.update({i: s["sha256"] for i, s in evidence["anchors"].items()})
            rows = data["records"]
            if len({r["id"] for r in rows}) != len(rows) or {r["id"] for r in rows} != set(expected):
                raise ValueError("index must cover exactly selected origins and anchors")
            for r in rows:
                p = Path(r["path"])
                if not p.is_absolute() or not p.is_file() or digest(p.read_bytes()) != expected[r["id"]] or r["sha256"] != expected[r["id"]]:
                    raise ValueError("selected private original missing or changed: " + r["id"])
            self.checks["private_originals"] = {"status": "PASS", "files": len(rows), "scope": "selected identities only"}
        except (OSError, ValueError, KeyError, TypeError) as e:
            self.error("PRIVATE_INDEX", "private-index", e)

    def boundary(self):
        allowed = {".md", ".json", ".py", ".txt"}
        for p in self.root.rglob("*"):
            if p.is_symlink():
                self.error("PATH_BOUNDARY", p.relative_to(self.root), "candidate contains symlink")
                continue
            if not p.is_file():
                continue
            name = p.relative_to(self.root).as_posix()
            if p.suffix.lower() not in allowed and p.name != "SHA256SUMS":
                self.error("PUBLICATION_BOUNDARY", name, "unexpected candidate file type")
            if p.suffix.lower() in {".md", ".json"}:
                try:
                    text = p.read_text(encoding="utf-8")
                except UnicodeError:
                    self.error("PUBLICATION_BOUNDARY", name, "unexpected non-UTF8 payload")
                    continue
                if re.search(r"(?:/home/|/mnt/c/Users/|[A-Za-z]:\\Users\\|PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{25})", text):
                    self.error("PUBLICATION_BOUNDARY", name, "private locator or known credential pattern")
        self.checks["public_boundary"] = "known patterns and selected file types only; license/history review pending"

    def links(self, virtual):
        def text_of(name):
            return virtual[name].decode("utf-8") if name in virtual else (self.root / name).read_text(encoding="utf-8")
        for required in REQUIRED_DOCS:
            if required not in virtual and not (self.root / required).is_file():
                self.error("LINK", required, "required entry document missing")
        paths = {p.relative_to(self.root).as_posix() for p in self.root.rglob("*.md")}
        paths.update(n for n in virtual if n.endswith(".md"))
        for name in sorted(paths):
            p = self.safe(name)
            if p is None:
                continue
            text = text_of(name)
            # Links inside fenced examples are not navigation.
            text = re.sub(r"(?ms)^\s*\x60{3}.*?^\s*\x60{3}\s*$", "", text)
            for match in re.finditer(r"\[[^\]\n]*\]\(([^)\s]+)(?:\s+[\"'][^\n]*?[\"'])?\)", text):
                href = match[1].strip("<>")
                parts = urlsplit(href)
                if parts.scheme or parts.netloc:
                    continue
                relative = PurePosixPath(name).parent / unquote(parts.path) if parts.path else PurePosixPath(name)
                normalized = []
                escaped = False
                for segment in relative.parts:
                    if segment == "..":
                        if not normalized:
                            escaped = True
                            break
                        normalized.pop()
                    elif segment != ".":
                        normalized.append(segment)
                if escaped:
                    self.error("LINK", name, "link escapes candidate: " + href)
                    continue
                target = PurePosixPath(*normalized).as_posix()
                local = self.safe(target, "LINK")
                if local is None:
                    continue
                if target not in virtual and not local.is_file():
                    self.error("LINK", name, "missing local link: " + href)
                    continue
                if not parts.fragment:
                    continue
                if local.suffix == ".json" and parts.fragment.startswith("/"):
                    self.pointer(target + "#" + parts.fragment)
                    continue
                if local.suffix != ".md":
                    self.error("LINK", name, "unsupported local anchor: " + href)
                    continue
                anchors = set()
                counts = {}
                content = text_of(target)
                for heading in re.finditer(r"(?m)^#{1,6}\s+(.+?)\s*#*\s*$", content):
                    slug = re.sub(r"[^\w\- ]", "", heading[1].lower()).replace(" ", "-")
                    index = counts.get(slug, 0)
                    counts[slug] = index + 1
                    anchors.add(slug + ("-" + str(index) if index else ""))
                anchors.update(re.findall(r'<a\s+(?:id|name)=["\']([^"\']+)', content))
                if unquote(parts.fragment) not in anchors:
                    self.error("LINK", name, "missing local heading: " + href)
        self.checks["local_links"] = "FAIL" if any(e["code"] == "LINK" for e in self.errors) else "PASS"


def aggregate(records, rules):
    states = {r["gate_status"] for r in records}
    for s in rules["default_priority"]:
        if s in states:
            if s == "UNKNOWN" and states & {"PASS", "PARTIAL"}:
                return rules["mixed_known_unknown"]
            return s
    return "UNKNOWN"


def derive(target, evidence, rules):
    by_id = {r["id"]: r for r in evidence["records"]}
    rows = []
    for row in rules["subsystems"]:
        rs = [by_id[i] for i in row["evidence_ids"]]
        rows.append({"id": row["id"], "gate_status": aggregate(rs, rules),
                     "evidence_ids": row["evidence_ids"],
                     "task_states": sorted({r["task_state"] for r in rs}),
                     "environments": sorted({r["environment"] for r in rs}),
                     "epistemic_statuses": sorted({r["epistemic_status"] for r in rs}),
                     "functionality": sorted({r["functionality"] for r in rs}),
                     "proof_scope": sorted({r["proof_scope"] for r in rs})})
    def proven(env):
        return any(r["environment"] == env and r["epistemic_status"] == "PROVEN" and r["gate_status"] == "PASS"
                   for r in evidence["records"])
    bsp = [r for r in evidence["records"] if r["id"].startswith("BSP_")]
    return {
        "schema_version": "1.0", "status": "CANDIDATE", "publication_status": "PUBLICATION_BLOCKED",
        "input_sha256": digest(encoded([target, evidence, rules])), "rules_version": rules["version"],
        "current_blocker": "B3_IMAGE4_INTERFACE_UNAVAILABLE" if by_id["B3"]["gate_status"] == "BLOCKED" else "UNKNOWN",
        "runtime_reproduction": evidence["independent_runtime_reproduction"],
        "delivery": {c["id"]: ("DEMONSTRATED" if by_id[c["id"]]["gate_status"] == "PASS" and
                     by_id[c["id"]]["environment"].startswith("DEVICE_VERIFIED_") and
                     by_id[c["id"]]["epistemic_status"] == "PROVEN" else rules["default_delivery"])
                     for c in target["delivery_criteria"]},
        "bsp": {"inventoried": len(bsp), "xnu_device_validated": sum(
            r["environment"] == "DEVICE_VERIFIED_XNU" and r["epistemic_status"] == "PROVEN" and
            r["gate_status"] == "PASS" for r in bsp)},
        "subsystems": rows, "claims": {"xnu_on_device": proven("DEVICE_VERIFIED_XNU"),
            "userland_on_device": proven("DEVICE_VERIFIED_USERLAND"), "gpu_on_device": proven("DEVICE_VERIFIED_GPU")}}
    

def render(status, evidence, register):
    def cell(value):
        return str(value).replace("|", "/").replace("\n", " ")
    lines = ["# Estado canônico candidato", "", "Gerado por validate-foundation.py. Não editar manualmente.",
             "", "PUBLICATION_BLOCKED. Validação documental; sem nova execução XNU, QEMU ou aparelho.",
             "", "Bloqueador: " + status["current_blocker"] + ". P0.7 concluiu pesquisa; o gate runtime continua BLOCKED.",
             "", "D1–D4: " + ", ".join(k + "=" + v for k, v in status["delivery"].items()) + ".",
             "", "BSP: " + str(status["bsp"]["inventoried"]) + " inventariados; " +
             str(status["bsp"]["xnu_device_validated"]) + " validados sob XNU no aparelho.",
             "", "| Subsistema | Gate | Execução da tarefa | Ambiente | Qualidade | Evidências |",
             "|---|---|---|---|---|---|"]
    for r in status["subsystems"]:
        lines.append("| " + " | ".join([cell(r["id"]), r["gate_status"], ", ".join(r["task_states"]),
            ", ".join(r["environments"]), ", ".join(r["epistemic_statuses"]),
            ", ".join("[" + i + "](EVIDENCE-INDEX.md#" + i.lower() + ")" for i in r["evidence_ids"])]) + " |")
    lines.extend(["", "Gate, tarefa, ambiente, qualidade e funcionalidade são eixos separados. PASS só tem o alcance da prova citada.",
                  "", "M9 PARTIAL agrega O1/build host documentados e O2/O3 sem recibos independentes no escopo auditado.",
                  "C04.3 PARTIAL não qualifica enforcement PMAP; Android observado não qualifica XNU.",
                  "", "[Contrato](manifests/target-contract.json) · [Evidências](EVIDENCE-INDEX.md) · [Regras](manifests/derivation-rules.json)",
                  "", "Input SHA-256: " + status["input_sha256"], ""])
    index = ["# Índice de evidências", "", "Gerado por validate-foundation.py. Não editar manualmente.",
             "", "Projeções documentais selecionadas, vinculadas a origem/commit/hash. Originais privados não são payload público.",
             "PROVEN descreve apenas o alcance citado. A W1 não reproduziu runtime nem qualificou segurança de implementação.",
             "", "## Fontes", "", "| ID | Autoridade | Repositório / caminho original | Commit | SHA-256 |",
             "|---|---|---|---|---|"]
    for identity, source in register["sources"].items():
        o = source["origin"]
        index.append("| [" + identity + "](evidence/source-register.json#/sources/" + identity + ") | " +
                     " | ".join(cell(v) for v in (source["authority"], (o["repository"] or "documento local") +
                         " / " + o["path"], o["commit"] or "sem commit", o["sha256"])) + " |")
    for r in evidence["records"]:
        index.extend(["", "## " + r["id"], "", r["claim"], "",
                      "Tarefa: " + r["task_state"] + "; ambiente: " + r["environment"] +
                      "; qualidade: " + r["epistemic_status"] + "; gate: " + r["gate_status"] + ".",
                      "Funcionalidade: " + r["functionality"] + ". Alcance: " + r["proof_scope"] + ".",
                      "", "Fontes: " + ", ".join("[" + i + "](evidence/source-register.json#/sources/" + i + ")" for i in r["source_ids"]) + ".",
                      "Âncoras privadas (identidade): " + (", ".join(r["anchor_ids"]) or "nenhuma") + ".",
                      "Limites: " + " ".join(r["limitations"]), ""])
    index.extend(["", "Perfil público verifica projeções e cópias selecionadas fixadas no lock. Perfil local reconfere hashes dos originais indicados explicitamente.",
                  "O lock não é assinatura nem impede adulteração coordenada de prova e lock; alterações de admissão exigem revisão humana da fonte.",
                  "Links externos não são consultados. Licenças e publicação continuam pendentes.", ""])
    return {"manifests/project-status.json": encoded(status),
            "STATUS.md": "\n".join(lines).encode("utf-8"),
            "EVIDENCE-INDEX.md": "\n".join(index).encode("utf-8")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--write", action="store_true", help="regenerate the three derived files only after all input checks")
    parser.add_argument("--profile", choices=("public", "local"), default="public")
    parser.add_argument("--private-index", help="explicit selected origins; required only for local profile")
    args = parser.parse_args()
    audit = Audit(args.root)
    data = [audit.json(n) for n in INPUTS]
    if all(isinstance(d, dict) for d in data):
        t, e, rules, register = data
        audit.authority(t, e)
        status_schema = audit.schemas(t, e)
        # Do not dereference ill-typed input, even if the schema file itself was tampered with.
        if not audit.errors:
            audit.lock(t, register)
            audit.contract(t, register)
            audit.provenance(t, e, rules, register)
            if args.profile == "local":
                audit.local(e, register, args.private_index)
            elif args.private_index:
                audit.error("PRIVATE_INDEX", "--private-index", "use explicit local profile for private reads")
            audit.boundary()
            outputs = render(derive(t, e, rules), e, register) if not audit.errors else {}
            for name in GENERATED:
                audit.safe(name)
            if outputs:
                status = json.loads(outputs[GENERATED[0]])
                for err in Draft202012Validator(status_schema).iter_errors(status):
                    audit.error("SCHEMA", GENERATED[0], err.message)
                if not args.write:
                    actual = audit.json(GENERATED[0])
                    if actual != status:
                        audit.error("STATUS_DERIVATION", GENERATED[0], "manual or stale status; regenerate from admitted evidence")
                    if actual is not None:
                        for err in Draft202012Validator(status_schema).iter_errors(actual):
                            audit.error("SCHEMA", GENERATED[0], err.message)
                    for name in GENERATED[1:]:
                        p = audit.safe(name)
                        if p and (not p.is_file() or p.read_bytes() != outputs[name]):
                            audit.error("GENERATED_DOCUMENT", name, "manual or stale generated document")
                audit.links(outputs if args.write else {})
            if args.write and not audit.errors:
                for name in outputs:
                    audit.safe_paths.pop(name, None)
                    audit.safe(name)
                if not audit.errors:
                    for name, content in outputs.items():
                        (audit.root / name).write_bytes(content)
                audit.checks["generated_files"] = "WRITTEN"
            elif not audit.errors:
                audit.checks["generated_files"] = "MATCH"
    else:
        if not audit.errors:
            audit.error("JSON", "inputs", "root objects required")
    result = {"ok": not audit.errors, "errors": audit.errors, "checks": audit.checks}
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 1 if audit.errors else 0


if __name__ == "__main__":
    sys.exit(main())
