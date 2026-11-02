# PMUX v0.2 release status

Date: 2026-11-02

## Current verified production result

- Mechanical G3 candidate: `12ac8b86cfd82d1439ae47d4b7b198b26f0e545a`
- Unified default-path validation: **47/47 PASS**
- Official whole-design EQY: **4/4 PASS**
- Official input hashes: **4/4 matched**
- Default and compatibility-explicit gates: identical for all four cases
- Native `synth_intel` and runner outputs: consistent

| Case | Default post-ABC9 `$lut` | Same-stage vendor primitive | DFF | map_cells result | Profile | EQY |
|---|---:|---:|---:|---|---|---|
| 1 | 182 | 0 | 222 | 180 `cycloneiv_lcell_comb` + 2 `$not` | stock | PASS |
| 2 | 0 | 15 `cycloneiv_lcell_comb` | 0 | 15 `cycloneiv_lcell_comb` | stock | PASS |
| 3 | 11 | 0 | 12 | 10 `cycloneiv_lcell_comb` + 1 `$not` | stock | PASS |
| 4 | 27 | 0 | 10 | 26 `cycloneiv_lcell_comb` + 1 `$not` | accepted_constant_table | PASS |

Complete structural rollback restores 288/25/13/36 `$lut` and
222/0/17/10 DFF for cases 1..4 respectively.

Generated plugin and gate hashes are recorded in each validation output. They
are not treated as cross-directory constants because generated artifacts can
embed build or source paths. Source commit, official input hashes, structural
counts and whole-design EQY are the portable acceptance identity.

## Status boundaries

- Current verified release target: **PASS** at 27 post-ABC9 `$lut` for case4.
- Historical case4 26 post-ABC9 `$lut` claim: **UNVERIFIED**; it is not used as a release claim.
- Complete historical P2 target: **UNVERIFIED**.
- External competition scoring and acceptance of explicit vendor primitives:
  **UNVERIFIED_EXTERNAL_SCORING**.
- eLinx project compilation/simulation: **NOT_EXECUTED** in this release run.
  See `ELINX_VALIDATION.md`.
