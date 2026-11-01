# Evidence manifest

All paths are relative to this directory.

| Path | Purpose |
|---|---|
| `AUDIT_REPORT.md` | Human decision and full audit result |
| `commands.txt` | Reproduction command record |
| `toolchain.txt` | Tool commits, versions and binary hashes |
| `SHA256SUMS` | Hash closure for all committed audit files except itself |
| `run_width_matrix.py` | Eight malformed-width mutations and Yosys control |
| `run_structural_audit.py` | Independent structural positive/negative fixtures |
| `run_fanout_audit.py` | Extra-consumer and unknown-port fanout audit |
| `check_priority_masks.py` | Exhaustive 256-vector priority semantics check |
| `run_dispatch_audit.py` | Default/explicit/rollback/conflict/family/state audit |
| `run_dual_consumer_lifecycle.py` | Native and runner lifecycle mutation matrix |
| `lifecycle_mutation_plugin.cc` | Test-only authorization-state mutator |
| `decision_consumer_reaudit_plugin.cc` | Independent consumer/order mutations |
| `case12_audit_report.cc` | Test-only case12 proof report plugin |
| `real_case12_adapter.py` | Compatibility-explicit random adapter |
| `cycloneiv_lcell_comb_formal.v` | Formal model used for case2 gate EQY |
| `evidence/summaries/*.json` | Machine-readable first/final-stage summaries |
| `evidence/*.log` | Complete compact console and assertion logs |
| `evidence/official-native-evidence.tar.gz` | Native scripts, IL/JSON, plans/stats, EQY configs/logs and PASS markers |
| `evidence/diff-name-status.txt` | Complete base-to-target changed-file list |
| `evidence/diff-check.txt` | Empty output means clean diff |
| `evidence/frozen-path-diff-exit.txt` | `0` means all frozen paths are unchanged |
| `evidence/identity-scan.txt` | Empty result from forbidden-identity scan |

Large per-seed scratch workdirs are not versioned. The two 1000-row JSON
summaries preserve every seed's synthesis/structure/EQY result, while console
logs preserve progress and terminal status.
