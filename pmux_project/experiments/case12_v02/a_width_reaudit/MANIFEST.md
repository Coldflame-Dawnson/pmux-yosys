# Audit evidence manifest

The complete local evidence root is:

`pmux_project/reports/a-case12-width-reaudit-4c43ab9/`

Tracked audit sources are under:

`pmux_project/experiments/case12_v02/a_width_reaudit/`

Portable evidence archives in that directory are:

- `first-stage-evidence.tar.gz`
- `random-1000-summaries.tar.gz`
- `unified-official-evidence.tar.gz`

Key evidence:

- `width-matrix/summary.json`: eight malformed-width variants plus the no-plugin Yosys control.
- `structural/summary.json`: 15 independent A structure cases.
- `fanout/summary.json`: preserved consumer and three conservative rejection cases.
- `priority-mask-summary.json`: exhaustive 256-vector priority-mask comparison.
- `analyzer-fast.log`, `analyzer-slow.log`: analyzer assertion totals.
- `decision-mutation.log`: 26/26 production mutation seam.
- `profile-snapshot/profile_snapshot_results.json`: 14/14 snapshot, generation, and lifecycle tests.
- `random-default-1000-retry/summary.json`: counted default-entry 1000-seed result.
- `random-explicit-case12-1000-retry/summary.json`: counted explicit-case12 1000-seed result.
- `unified-official/`: independently generated four official gates, scripts, plans, stats, JSON, RTLIL, logs, EQY work directories, and PASS files.
- `case34-regression/summary.json`: dedicated accepted case3/case4 no-regression run.

The two non-retry random directories document an invalid relative-tool-path invocation. They are `NOT_RUN` and are not used as evidence.
