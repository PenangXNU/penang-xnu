# Work order — <ID> <short title>

- **Owner / requester:**
- **Baseline commit:**
- **Mode:** LOCAL_ONLY | CI_ALLOWED | DEVICE_APPROVED (name the exact approved operation)
- **Branch:** `wo/<id>-<slug>`

## Mission

What must be true when this work order is done, in one or two sentences.

## Scope

- In scope:
- Out of scope (explicit):

## Constraints

Repeat any hard rule from AGENTS.md that is especially relevant (Apple material, security policy, device operations).

## Acceptance gates

| Gate | Criterion | Evidence class expected |
|---|---|---|
| G0 | Baseline confirmed (commit, clean tree) | VERIFIED |
| G1 | | |
| G2 | Positive and negative tests PASS | TESTED |
| Gn | Handoff written | — |

## Deliverables

Code, tests, generated status (if affected), handoff file.
