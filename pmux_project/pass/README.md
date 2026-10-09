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
equals the committed edit count, `changed_structures` equals the structural
edit count, and `stats.v02_case34.enabled` matches the requested mode.
Official LUT measurements must not use the old one-line `synth_intel` command
as their purported optimized run.

## v0.2 case3/case4 accepted production mode

The default `pmux_opt` command enables the accepted v0.2 structural rules. It calls A's
`ProductionCase34Analyzer` at the same audited insertion point and accepts a
structural edit only when every proof obligation, reset/enable/hold contract,
cell snapshot and `keep` check passes. It does not inspect a file name, module
name, source location or case number. `-v02-case34` remains accepted as a
compatibility spelling; `-no-v02-case34` is the explicit baseline/diagnostic
rollback and never participates in normal production selection.

The frozen runner uses this mode by default. `pmux_opt_abc9` selects the
accepted custom mapping profile only when the complete set of generated
`pmux_case34_lut4` cells is byte-for-byte equal to the immutable snapshot
published by the immediately preceding proof-gated rewrite. The snapshot binds
module and cell identity, type, every parameter, attribute, port, connection,
bit order and target-cell multiplicity. A separate generation token and
lifecycle monitor bind the authorization to the actual cell instances that
existed at publication: deleting an authorized cell permanently invalidates
that generation even if a byte-identical cell with the same name is rebuilt.
The authorization flag, snapshot, generation and lifecycle state are consumed
before any mapping side effect. Missing fields, an invalidated lifecycle or any
post-authorization content change selects the stock profile. A pre-existing or
recreated cell name cannot select the profile. Unmatched designs and decision-cone-only rewrites
retain the stock ABC9 profile. Constant-table cells are lowered by the adjacent
`pmux_case34_lut_map.v` even on a rejected authorization so that the conservative
stock ABC9 fallback remains executable; only the custom ABC9 script requires a
matching authorization snapshot. The adjacent accepted ABC9 script is also
selected by default; `--abc-script` can reproduce an explicitly archived copy:

```bash
python3 pmux_project/pass/run_synth_intel_pmux.py \
  --yosys /path/to/yosys --plugin pmux_project/pass/pmux_opt.so \
  --source test4.v --top test4 --stage post_abc9 \
  --abc-script pmux_project/experiments/case34_v02b/evidence/generated/abc.script \
  --plan out/plan.json --stats out/stats.json \
  --json out/post_abc9.json --rtlil out/post_abc9.il \
  --log out/post_abc9.log --script out/post_abc9.ys
```

The accepted default was admitted only after the unified offline cost and EQY
gates. `run_production_validation.py` uses `--no-v02-case34` for an explicit
baseline, synthesizes the default production path independently, and checks
the production gate against the official RTL. Current strict results are
case3 11 LUT/12 DFF and case4 27 LUT/10 DFF; both gates pass EQY and meet their
strict P2 oracles.
A nonmatching design records `no_v02_case34_candidate`, has an empty
`structural_edits` array, and receives no structural write.

The snapshot contract and real native `synth_intel` mutation tests can be run
with:

```bash
python3 pmux_project/experiments/case34_v02b/scripts/check_profile_consumption.py
python3 pmux_project/experiments/case34_v02b/scripts/run_profile_snapshot_tests.py \
  --official-dir /path/to/competition_case
```

The second command covers a valid first consumption, LUT parameter bit and
width changes, port/type/attribute changes, target removal/addition, a missing
snapshot, byte-identical same-name reconstruction before consumption in both
consumer paths, and a full target-cell replay after authorization consumption.

For a proved decision cone, B first attempts a bounded proof-data-only
materialization over support bits and proved comparator outputs: constants,
one atom (or its inverse), and two-atom AND/OR/XOR. A candidate is accepted
only when it matches the sink's returned value on every returned care row;
otherwise B materializes A's complete topological DAG. Care-zero rows are
never treated as an equality claim: their freedom is used only after A has
proved that D is unobserved there and that reset/enable/hold behavior is
preserved. The consumer never changes a sequential Q, clock, reset or enable.

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

A owns comparator-width/sign rejection, unsupported-cell propagation,
no-opportunity details, `fact_conflict`, and the separate cell/bit/event budget
reason codes. B forwards A's entry and per-bit reason strings into the machine
statistics without changing their meaning, but only `proved` conclusions may
enter the rewrite planner. The test-only codes and modes are not part of the
production analyzer contract.
