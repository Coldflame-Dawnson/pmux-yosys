# PMUX R1' read-only analyzer

v0.2-B 的整 `$pmux` 分支冗余证明接口、精确重叠语义、原因码与 case3
实测记录见 `branch_redundancy_v02b.md`。

This directory is owned by role A. It implements the frozen
`EntryRequest -> EntryAnalysisResult` interface and a read-only v0.2-B
diagnostic command; neither registers a production rewrite pass nor modifies
RTLIL.

## Build and test

```sh
make -C pmux_project/analyzer test
make -C pmux_project/analyzer test-slow
```

The regular target covers rules, validation, proof tracing, determinism,
read-only fingerprints, and the cell/bit budget boundaries. `test-slow` also
drives the real work queue through the 262,144-event limit; it is intentionally
slower than the rule suite.

## Frozen rule surface

Supported propagation is limited to:

- SigMap-normalized constants and direct aliases;
- `$not`, `$and`, and `$or` deterministic bit rules;
- a known-select `$mux`, only between `Y` and the selected data input;
- uniquely determined `$logic_not/$logic_and/$logic_or` facts;
- uniquely determined `$reduce_and/$reduce_or/$reduce_bool` facts;
- strict `(X == C) = 1` and `(X != C) = 0` reverse inference, where widths,
  signedness, bit identity, and a fully binary constant are unambiguous;
- `ConstEval` forward evaluation after all relevant input facts are known.

Everything else, including XOR/XNOR, arithmetic, shifts, ordering compares,
sequential logic, memories, black boxes, and multiple drivers, is a proof
boundary. No SAT reasoning is present.

## Stable result and bit reasons

Entry-level reasons are `fixed_point`, `fact_conflict`,
`budget_cells_exhausted`, `budget_bits_exhausted`,
`budget_events_exhausted`, and the `invalid_*` validation reasons in
`readonly_analyzer.cc`.

Bit-level reasons are `proved`, `original_xz`, `primary_input`, `undriven`,
`multiple_driver`, `sequential_boundary`, `memory_boundary`,
`blackbox_boundary`, `unsupported_cell`,
`comparison_semantics_ambiguous`, `fixed_point_unknown`, the three budget
reasons, and `fact_conflict`.

## Integration hand-off

B should construct the four exact frozen guard forms, preserve the complete
physical-port snapshot, call `make_readonly_entry_analyzer()`, and consume only
proved nonconstant bit conclusions. B remains responsible for planning,
conflict resolution, atomic port rewriting, pass registration, and the
`synth_intel` insertion point.

C should reproduce these rule cases in the unified black-box suite: all four
guards and rejection of enlarged guards; every supported forward/reverse rule
and its forbidden counterpart; strict EQ/NE cases; independent per-bit
failure; constants and X/Z; unsupported, sequential, memory, black-box, and
multiple-driver boundaries; fact conflict; all three budget limits; proof
traceability and shortest stable selection; repeated-run determinism; and
before/after ILANG identity. The self-test intentionally stays here rather
than modifying C-owned `pmux_project/tests/`.
