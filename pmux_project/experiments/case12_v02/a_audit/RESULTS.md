# A-side case1/case2 first-stage audit

## Verdict

`BLOCKED`

The frozen B commit `504df2a389bf20d09e3bdaae64528caacc0c7bcf`
must not be enabled by default or accepted into the final G3 integration. The
independent structural suite contains 15 tests: 13 pass and two required
negative tests are falsely accepted and committed.

This is a first-stage structural audit only. The stop-on-false-accept rule was
applied, so no 1000-seed regression and no final official whole-design
acceptance run was performed.

## Frozen identity

- Remote branch: `feature/b-v02-case12-production`
- Audited commit: `504df2a389bf20d09e3bdaae64528caacc0c7bcf`
- Parent: `b8368b0f3e2d55a2bb429296a3aea859eb4f2049`
- A audit branch: `feature/a-v02-case12-audit-504df2a`
- Required Yosys ancestor: `9f75ca1f9834a39a863915b5dae0c7b1e33533bc`
- Remote `ls-remote` returned the audited commit exactly before the branch was
  created.

The official input hashes were checked independently:

| input | SHA-256 |
|---|---|
| `test1.v` | `2D2DBA011B0E8799D0DE3790B69B7F01C6EB6AC665C4321B57B0378A4972FD13` |
| `test2.v` | `E43C9B437071488551CAF3121E227E86F4FE3190751F245BEAC3B7ECC3FDB146` |
| `test3.v` | `1551B9212622CCEF6019D6E77DE2C43DC8C80D267957B55A42121F34A08780E9` |
| `test4.v` | `C0C5042928BD6BF568E2B7FB2193F2B6DB9C4193BA3A5378FC50A331D43422D7` |

## Blocking finding 1: a five-branch priority chain is partially rewritten

Fixture:
`evidence/structural-final/fixtures/priority_five_branch_must_reject.v`

Frozen expectation: a priority chain with five branches is outside the exact
four-branch rule and must produce no candidate and no write.

Observed summary:

```text
pmux_opt: v02_case12 paired=0 priority=1 committed=1 removed_cells=4 added_cells=3
Warning: Wire top.\m3 is used but has no driver.
```

The analyzer considers every `$mux` as a possible outer node and validates four
nodes below it, but does not prove that the selected node is the true outermost
node of the chain. In this fixture it accepts the inner four nodes of a
five-node chain. The consumer then removes those four cells, while the fifth
outer mux still consumes `m3`; Yosys reports that `m3` has no driver. This is
not only a rule-boundary mismatch: it creates an unsafe rewritten netlist.

Relevant production locations, unchanged by this audit:

- Candidate search: `pmux_project/analyzer/case12_proof_analyzer.cc:209-243`
- First dangerous removal: `pmux_project/pass/case12_rewrite.cc:93-94`
- Commit call: `pmux_project/pass/case12_rewrite.cc:128-129`

Expected summary: `(paired=0, priority=0, committed=0, removed=0, added=0)`.
Actual summary: `(0, 1, 1, 4, 3)`.

## Blocking finding 2: comparator output module fanout is not rejected

Fixture:
`evidence/structural-final/fixtures/paired_comparator_module_output_fanout.il`

Frozen expectation: all comparator-output fanout must be covered, otherwise the
paired-PMUX candidate is ambiguous and must be rejected.

Observed summary:

```text
pmux_opt: v02_case12 paired=1 priority=0 committed=1 removed_cells=2 added_cells=10
```

The fixture exposes one equality-decode result as a module output in addition
to using it as a PMUX select. The analyzer builds a map from equality outputs
to decoders but never records or validates all users of those outputs before
setting `proven_all_obligations=true`. The paired rewrite therefore commits
despite the frozen fanout obligation being unproved.

Relevant production locations, unchanged by this audit:

- Equality output map without user/fanout closure:
  `pmux_project/analyzer/case12_proof_analyzer.cc:100-113`
- Proof marked complete: `pmux_project/analyzer/case12_proof_analyzer.cc:174-179`
- First dangerous PMUX removals: `pmux_project/pass/case12_rewrite.cc:62-63`

Expected summary: `(0, 0, 0, 0, 0)`.
Actual summary: `(1, 0, 1, 2, 10)`.

This finding is a proof-contract false accept. It is not presented as a
demonstrated functional mismatch for this particular module-output tap because
the comparator cells themselves are retained.

## Passing scope

B's own five quick fixtures pass. In the independent suite, the following
boundaries pass: priority widths 1/12/32, paired widths 1/12/32, three-branch
rejection, nonzero-default rejection, intermediate module/cell fanout
rejection, missing or duplicate decode rejection, and swapped paired data-slice
rejection.

Passing neighbors do not waive the two false accepts above.

## Required disposition

B must issue a new full commit hash after, at minimum:

1. proving that an accepted four-mux chain has no unaccounted outer consumer;
2. closing the equality-decoder fanout obligation or conservatively rejecting
   any extra cell/module-output fanout;
3. adding both counterexamples to B's permanent negative suite; and
4. rerunning the first-stage audit before any long regression or official
   acceptance claim.

No production source, official RTL, test oracle, or synthesis insertion point
was modified by A.
