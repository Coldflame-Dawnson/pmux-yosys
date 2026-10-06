# case3 v0.2 offline ablation

This directory is a test-only experiment. It is not linked into `pmux_opt`,
does not infer proofs, and must not be used on arbitrary designs. The plugin
accepts only the frozen case3 RTLIL snapshot after exact cell, type, parameter,
port and signal checks.

Variants:

- `a`: bypass and remove `$procmux$10`;
- `b`: delete branch 0 of `$procmux$18` and convert the remaining branch to a
  one-bit `$mux`;
- `c`: bypass bits 0..2 of `$procmux$27` and retain bit 3 as a one-bit `$mux`;
- `ab` and `abc`: apply the named transformations atomically.

Build and generate all five structural variants:

```bash
make YOSYS_CONFIG=/path/to/yosys-config
python3 run_ablation.py \
  --yosys /path/to/yosys \
  --plugin ./case3_v02_ablate.so \
  --before /path/to/case3/baseline/01_pre_pmux.il \
  --out out
```

The driver stores a before/after RTLIL pair, JSON netlist, manifest, command,
log and SHA-256 values for every variant. It also checks the exact resulting
structure and deliberately reapplies variant A to an already-transformed
snapshot; that negative run must fail before writing a manifest or after
netlist. ABC9 and EQY measurement belong to C's independent verification
stage.
