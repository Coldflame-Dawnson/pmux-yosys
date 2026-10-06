# B v0.2 whole-PMUX-branch rewrite handoff

## Scope

This change implements only the structural consumer for an already-proven
whole-branch redundancy certificate. It does not infer equivalence. The new
path is disabled by default, and `-v02-branch-delete` is a no-op until A's real
certificate provider is connected.

The current shared A interface has no `BranchProof` type. Therefore the only
active certificate source in this commit is the explicitly test-only
`-test-branch-proof <cell> <branch>` option. Its machine mode is
`testing_v02_branch_mock`; it must not be counted as production proof or LUT
evidence.

## Structural behavior

- At least two branches survive: compact `B` and `S`, preserve survivor order,
  and update `S_WIDTH`.
- Exactly one branch survives: convert the cell from `$pmux` to equal-width
  `$mux`.
- No branch survives: connect `Y=A` and remove the cell.
- Partial/unproved certificates are rejected.
- `keep` and Yosys selection are respected.
- All target parameters and full A/B/S/Y snapshots are validated before the
  first branch mutation.
- Mixed physical-bit and whole-branch candidates are conservatively rejected
  before either batch writes RTLIL; a future A integration must deliberately
  replace this guard with a unified two-phase transaction.

## Test result

The deterministic structural suite reports 16/16 PASS:

1. first branch deletion;
2. middle branch deletion;
3. last branch deletion;
4. multiple deletions reducing to `$mux`;
5. all branches deleted and A bypassed;
6. single-branch PMUX bypassed;
7. v0.2 enabled with no provider is a no-op;
8. default-disabled path is a no-op;
9. partial proof rejection;
10. invalid branch index rollback;
11. snapshot mismatch rollback;
12. conflict rollback;
13. duplicate-target deduplication;
14. `keep` rejection;
15. selection rejection;
16. frozen case3 V2-B structural replay.

Frozen case3 input SHA-256:
`baa2a34eb6fd25db3950defbc141a1477d7114b38eee971d9b02cca12d84af1e`

Frozen case3 expected JSON SHA-256:
`3013f07a9202646a85b7dd1f9e8d0f6dd6d132d75c1009e6a5f9aa7895db0266`

The generic rewriter deletes branch 0 of `$procmux$18`, changes `S_WIDTH` from
2 to 1, converts `$pmux` to `$mux`, and produces a module JSON structure equal
to B's frozen V2-B experiment.

## Machine contract

Plan schema 1 retains `edits` and adds `branch_edits`. Each branch edit records
the module, cell, deleted branch indices, proof IDs, removed B ranges, removed
S indices, width, old/new `S_WIDTH`, old/new type, and transformation kind.

Stats schema 1 retains existing fields and adds `branch_rewrite`, including
certificate, rejection, duplicate, conflict, planned/committed branch and
resulting-cell-kind counters.

## Not yet claimed

- No real A certificate is consumed yet.
- No new EQY or ABC9 result is claimed by B.
- This branch must not be merged into `integration/g3` until A is connected and
  C completes the real 100/1000 plus official-case acceptance matrix.
