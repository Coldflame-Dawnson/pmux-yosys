# PMUX v0.2-B structural rewrite tests

These tests exercise B's generic whole-branch rewriter with test-only mock
certificates. They do not claim that a branch is functionally redundant; A is
responsible for producing that proof and C is responsible for EQY and ABC9
measurement.

The matrix covers first, middle and last branch removal, multiple removals,
`$pmux` to `$mux`, complete `A` bypass, partial-proof rejection, invalid branch
metadata, duplicate targets, conflicts, snapshot rollback, `keep`, and Yosys
selection.

Run from the repository root:

```bash
make -C pmux_project/pass YOSYS_CONFIG=/path/to/yosys-config
python3 pmux_project/tests/pmux_v02_b/run_branch_delete_tests.py \
  --yosys /path/to/yosys \
  --plugin pmux_project/pass/pmux_opt.so \
  --out /tmp/pmux-v02-b-results
```

To compare the generic rewrite with B's frozen case3 V2-B experiment, also
provide the experiment's `before.il` and `after.json`:

```bash
python3 pmux_project/tests/pmux_v02_b/run_branch_delete_tests.py \
  --yosys /path/to/yosys \
  --plugin pmux_project/pass/pmux_opt.so \
  --out /tmp/pmux-v02-b-results \
  --case3-before /path/to/case3-v2-b/before.il \
  --case3-expected-json /path/to/case3-v2-b/after.json
```
