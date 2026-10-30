# A second re-audit of case1/case2 production rules

Date: 2026-10-30

## Verdict

`BLOCKED`

The re-audited B commit is:

```text
da516119e0f59e40d678ead0014a8dda83b78366
```

The two defects reported against `504df2a389bf20d09e3bdaae64528caacc0c7bcf`
are fixed. However, the case1 comparator decoder still accepts an inconsistent
`$eq` width parameter, proves the paired-PMUX candidate, and commits the
rewrite. The following Yosys `check` then terminates with exit code 139.

Under the frozen audit contract, a comparator width/parameter ambiguity is a
mandatory conservative rejection. This false accept blocks `AUTHORIZED`, the
1000-seed runs, final four-case whole-design EQY, default enablement, and final
G3 integration.

## Frozen identity

- B branch: `feature/b-v02-case12-production`
- B commit: `da516119e0f59e40d678ead0014a8dda83b78366`
- Direct parent: `8a8715973f42d4d4b487784578b23413ae24c9c2`
- Repair base: `504df2a389bf20d09e3bdaae64528caacc0c7bcf`
- A branch: `feature/a-v02-case12-reaudit-da51611`
- Remote `ls-remote` returned the B commit exactly before the A branch was
  created.

## Confirmed repairs

B's seven structural fixtures pass. A's previous 15-case independent suite
also passes 15/15, including:

- a five-branch priority chain now produces zero candidates and zero writes;
- comparator decode exposed as a module output now produces zero candidates
  and zero writes;
- widths 1/12/32 remain accepted for both valid rule families;
- missing/duplicate codes, swapped B slices, nonzero defaults and extra
  intermediate fanout remain rejected.

The two original blockers are therefore closed for this commit.

## New blocking finding: `$eq` parameter/port width mismatch is accepted

Minimal fixtures:

```text
evidence/paired_clean_valid.il
evidence/paired_clean_awidth_mismatch.il
```

The files are identical except that the first equality comparator changes:

```text
parameter \A_WIDTH 3
```

to:

```text
parameter \A_WIDTH 4
```

while its actual `A` connection remains the three-bit `state` signal. The
mutated comparator's declared width no longer matches its physical port.

Expected result:

```text
paired=0 priority=0 committed=0 removed_cells=0 added_cells=0
```

Actual result before the crash:

```text
pmux_opt: v02_case12 paired=1 priority=0 committed=1 removed_cells=2 added_cells=10
```

The process then exits with code 139 during `check`.

Root cause: `decode_eq()` checks signedness and the sizes of the connected
`SigSpec` values, but does not validate `A_WIDTH`, `B_WIDTH` or `Y_WIDTH`
against the corresponding ports before treating the comparator as a valid
decode. See `pmux_project/analyzer/case12_proof_analyzer.cc:51-64`. The proof is
marked complete at lines 210-216. The consumer snapshots only the two PMUX
cells, then creates replacement muxes and removes the PMUX pair; the first
dangerous removal is `pmux_project/pass/case12_rewrite.cc:62`, called from line
119.

This is not a claim that arbitrary malformed RTLIL must be optimized. It is the
opposite: the frozen proof boundary explicitly requires ambiguous or
inconsistent comparator width semantics to be rejected with zero writes.

## Other completed first-stage checks before the blocker

| Check | Result |
|---|---|
| B structural fixtures | 7/7 PASS |
| A original structural suite | 15/15 PASS |
| Analyzer fast | 2116 / 29 / 60 / 50 / 81 PASS |
| Analyzer slow | 2118 / 29 / 60 / 50 / 81 PASS |
| Decision proof mutation seam | 26/26 PASS |
| Profile snapshot/generation/lifecycle/one-shot tests | 14/14 PASS |
| Official case1 insertion point | paired=1, committed=1, removed=2, added=18, check clean |
| Official case2 insertion point | priority=1, committed=1, removed=4, added=15, check clean |
| Default case1 fingerprint | before/after identical |
| Default case2 fingerprint | before/after identical |

These passing results do not override the comparator false accept.

## Required B action

Before another A audit, B must:

1. validate that `$eq` has all required width and signedness parameters;
2. require `A_WIDTH == width(A)`, `B_WIDTH == width(B)` and `Y_WIDTH == width(Y)`;
3. reject missing, nonpositive, inconsistent, ambiguous or unsupported width
   declarations before emitting a proof;
4. add permanent negative fixtures for A/B/Y width mismatch and missing width
   parameters, plus signed and `x/z` boundaries;
5. prove that each rejection produces zero candidate and zero write; and
6. provide a new full commit hash for a fresh independent A audit.

A did not modify production code, the frozen interface, official RTL, the
synthesis insertion point, or B's test expectations.
