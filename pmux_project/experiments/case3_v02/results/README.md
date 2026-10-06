# B case3 v0.2 structural ablation results

These files are B's test-only structural handoff to C. They are not production
rules and do not establish functional equivalence or LUT improvement.

Input snapshot SHA-256:

`baa2a34eb6fd25db3950defbc141a1477d7114b38eee971d9b02cca12d84af1e`

| Variant | Transformation | Yosys check | Structural assertions |
|---|---|---|---|
| `a` | delete and bypass `$procmux$10` | PASS | PASS |
| `b` | remove branch 0 of `$procmux$18`; retain branch 1 as `$mux` | PASS | PASS |
| `c` | bypass `$procmux$27` bits 0..2; retain bit 3 as `$mux` | PASS | PASS |
| `ab` | A + B | PASS | PASS |
| `abc` | A + B + C | PASS | PASS |

The `negative_snapshot` run reapplies A after A has already removed its target.
It exits nonzero with `missing cell $procmux$10` and writes neither a manifest
nor an after netlist. This verifies that a requested transformation is not
partially committed when its frozen snapshot does not match.

`summary.json` is the machine-readable source of hashes and verdicts. Each
variant directory contains the exact before/after RTLIL, after JSON, action
manifest, command and logs. C must independently run EQY and the frozen ABC9
measurement before any candidate can be accepted.
