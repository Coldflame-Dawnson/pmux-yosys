# `pmux_opt` Pass and machine interface

## Build and invocation

Build the plugin with the frozen Yosys 0.69 `yosys-config`:

```bash
make -C pmux_project/pass YOSYS_CONFIG=/path/to/yosys-config
```

The Pass consumes RTLIL after `proc` and before common `techmap`. A standalone
snapshot invocation is:

```text
read_rtlil before.il
pmux_opt -write-plan plan.json -write-stats stats.json
write_rtlil after.il
```

The command name is `pmux_opt`. Its default analyzer is A's read-only proof
engine created by `make_readonly_entry_analyzer()`. B accepts a determined bit
only when its reason is `proved` and its proof ID is valid. `-stub-unknown`
retains the pre-integration no-write path. All `-stub-*` and `-test-*` switches
are test-only and must not be used to claim functional optimization or LUT
benefit.

## Experimental v0.2 whole-branch rewriter

`-v02-branch-delete` enables B's whole-branch rewrite path, but it is currently
a no-op without a certificate provider. A's shared interface does not yet
publish `BranchProof`, so `-test-branch-proof <cell> <index>` is the only
available source and is explicitly test-only. The rewriter does not infer
equivalence.

Given complete certificates, the batch validates every cell type, parameter,
port and full-port snapshot before its first write. It removes arbitrary branch
indices while retaining the original order of all surviving branches:

- two or more survivors: shrink `B`, `S`, and `S_WIDTH`;
- one survivor: convert `$pmux` to an equal-width `$mux`;
- no survivors: connect `Y=A` and remove the cell.

Partial proofs are rejected. Duplicate targets are deduplicated. Invalid branch
indices, conflicts, and snapshot mismatches abort the branch batch with zero
branch writes. `keep` and the active Yosys selection are enforced before a
test certificate can enter the batch. Until the shared A interface supports a
unified two-phase transaction, simultaneous physical-bit and whole-branch
candidates are conservatively rejected with
`mixed_bit_and_branch_batch_unsupported` before either batch writes RTLIL.

## Audited `synth_intel` entry

The frozen Yosys binary does not contain the source-tree edit to
`synth_intel.cc`. Merely selecting a runner's `real` mode therefore does not
execute this plugin. `run_synth_intel_pmux.py` loads `pmux_opt.so` explicitly
and splits `synth_intel` at the `map_ffram` label. It reproduces the original
`map_ffram` commands, inserts `pmux_opt` after `memory_map; opt -undriven
-fine`, runs `opt_clean`, and then resumes the original common techmap and the
remaining Intel flow.

For the post-ABC9/pre-vendor-cell snapshot:

```bash
python3 pmux_project/pass/run_synth_intel_pmux.py \
  --yosys /path/to/yosys --plugin pmux_project/pass/pmux_opt.so \
  --source test3.v --top test3 --stage post_abc9 \
  --plan out/plan.json --stats out/stats.json \
  --json out/post_abc9.json --rtlil out/post_abc9.il \
  --log out/post_abc9.log --script out/post_abc9.ys
```

Use `--stage full` for the complete flow. The wrapper fails unless the log
shows exactly one real `PMUX_OPT` execution in this order:

```text
MEMORY_MAP -> PMUX_OPT -> OPT_CLEAN -> common TECHMAP -> ABC9
```

It also requires real-mode plan/stat files and verifies that `changed_bits`
equals the committed edit count. Official LUT measurements must not use the
old one-line `synth_intel` command as their purported optimized run.

## Plan JSON schema 1

`-write-plan <file>` writes one document. `edits` contains only physical bits
that were actually committed; an aborted all-batch precheck therefore produces
an empty array.

```json
{
  "schema": 1,
  "producer": "pmux_opt",
  "mode": "real",
  "commit": "ok",
  "commit_reason": null,
  "edits": [
    {
      "module": "top",
      "cell": "target",
      "port": "B",
      "bit": 3,
      "value": "1",
      "old": {"kind": "signal", "wire": "data", "offset": 0},
      "branch": 1,
      "branch_bit": 0,
      "proof_id": 7,
      "source_entry": "stable-entry-id"
    }
  ],
  "branch_edits": [
    {
      "module": "top",
      "cell": "target",
      "deleted_branches": [0],
      "proof_ids": [1000000],
      "removed_b_ranges": [{"branch": 0, "offset": 0, "width": 2}],
      "removed_s_indices": [0],
      "width": 2,
      "old_s_width": 2,
      "new_s_width": 1,
      "old_type": "$pmux",
      "new_type": "$mux",
      "transformation": "pmux_to_mux"
    }
  ],
  "abandoned_entries": [
    {"entry": "stable-entry-id", "reason": "rewrite_conflict"}
  ]
}
```

Module, cell and wire names use the same unescaped spelling as Yosys
`write_json`. `bit` is a zero-based physical A/B-port bit. `branch` is an
integer only for `$pmux.B`; it is `null` for `$pmux.A` and ordinary `$mux`.
For `$pmux.B`, `branch == bit / WIDTH` and `branch_bit == bit % WIDTH`.

## Statistics JSON schema 1

`-write-stats <file>` writes:

- `changed_bits`: actual committed physical bits, equal to `edits.length`;
- `reason_codes` and `reason_counts`: stable machine-readable diagnostics;
- `scan`: module, candidate-cell, valid/invalid-cell, entry and target-bit counts;
- `analysis`: entry statuses, analyzer resource counters, proven 0/1, unknown
  bits, and `sat_queries` (fixed to zero in v0.1);
- `rewrite`: candidate, accepted, committed, deduplicated, conflict, abandoned,
  rejected-entry, rejected-unproved-conclusion and original-constant counts;
- `branch_rewrite`: certificate, partial/unproved rejection, duplicate,
  conflict, planned/committed branch and resulting-cell-kind counts;
- `resources`: microseconds and KiB, with units recorded explicitly.

`candidate_bits` are proven nonconstant replacement candidates.
`accepted_bits` are candidates remaining after conflict resolution and before
the all-batch snapshot precheck. `committed_bits` and top-level `changed_bits`
are physical bits written after a successful precheck.

## Exit and fallback behavior

- No candidate or only unknown results: success, empty edits, zero changes.
- Structurally invalid or `keep` cell: warning, stable reason code, cell skipped.
- `fact_conflict` or invalid analyzer result: current entry rejected.
- `budget_exhausted`: complete proven bits may remain; incomplete bits are
  unknown. Limits are inclusive: 512 cells, 8192 normalized bits and 262144
  events are allowed; attempting the next item emits A's corresponding
  `budget_*_exhausted` reason while the recorded usage remains at the limit.
- `rewrite_conflict`: all entries involved at that physical bit are abandoned;
  independent entries may still commit.
- Any full-port snapshot mismatch: whole batch aborts before the first RTLIL
  write; success with `commit="aborted"`, empty edits and zero changes.
- Bad command syntax or failure to write a requested JSON file: command error
  and nonzero Yosys exit.

## Reason-code ownership

B currently emits structural and commit codes including `keep_attribute`,
`missing_width`, `invalid_width`, `missing_port`, `port_width_mismatch`,
`missing_s_width`, `invalid_s_width`, `port_width_overflow`,
`invalid_rewrite_metadata`, `rewrite_conflict`, `snapshot_mismatch`,
`bit_snapshot_mismatch`, `missing_cell_at_commit`, `cell_type_changed`, and
`missing_port_at_commit`. `original_binary_constant` and `original_xz` are
defensive result filters.

The v0.2 branch batch additionally owns `branch_partial_proof`,
`branch_unproved`, `branch_target_conflict`, `invalid_branch_rewrite_metadata`,
`missing_branch_cell_at_commit`, `branch_cell_type_changed`,
`branch_parameter_snapshot_mismatch`, `branch_port_snapshot_mismatch`, and
`keep_attribute`. Codes prefixed with `test_` are fault-injection evidence and
are not production proof outcomes.

A owns comparator-width/sign rejection, unsupported-cell propagation,
no-opportunity details, `fact_conflict`, and the separate cell/bit/event budget
reason codes. B forwards A's entry and per-bit reason strings into the machine
statistics without changing their meaning, but only `proved` conclusions may
enter the rewrite planner. The test-only codes and modes are not part of the
production analyzer contract.
