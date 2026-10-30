# Manifest

All paths are relative to this directory.

| Path | Purpose |
|---|---|
| `RESULTS.md` | Re-audit verdict, closed findings and new blocker |
| `commands.txt` | Reproduction commands |
| `toolchain.txt` | Frozen source commits and executable hashes |
| `run_structural_audit.py` | Independent 15-case structural oracle |
| `evidence/b-validation.log` | B 7-case structural output |
| `evidence/a-structural.log` | A 15-case structural output |
| `evidence/structural/` | A fixtures and machine-readable summary |
| `evidence/paired_clean_valid.il` | Clean paired-PMUX control fixture |
| `evidence/paired_clean_awidth_mismatch.il` | One-parameter blocking mutation |
| `evidence/paired_clean_awidth_mismatch.log` | Production acceptance before crash |
| `evidence/paired_clean_awidth_mismatch.result.txt` | Command, expected/actual and exit code |
| `evidence/analyzer-fast.log` | Analyzer fast output |
| `evidence/analyzer-slow.log` | Analyzer slow output |
| `evidence/decision-mutation.log` | Production mutation seam output |
| `evidence/profile-snapshot/` | Snapshot, generation, lifecycle and replay checks |
| `evidence/case1-insertion.log` | Official case1 insertion-point check |
| `evidence/case2-insertion.log` | Official case2 insertion-point check |
| `evidence/case1-default.log` | Default-path case1 run |
| `evidence/case2-default.log` | Default-path case2 run |
| `SHA256SUMS` | Integrity list for all other package files |

No production source or official RTL is included or modified by this package.
