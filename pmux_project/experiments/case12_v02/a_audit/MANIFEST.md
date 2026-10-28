# Manifest

All paths are relative to this directory.

| Path | Purpose | Evidence status |
|---|---|---|
| `RESULTS.md` | First-stage verdict, findings, boundaries and disposition | GENERATED |
| `commands.txt` | Short reproduction commands | GENERATED |
| `toolchain.txt` | Frozen source commits and executable hashes | GENERATED |
| `run_structural_audit.py` | Independent fixture generator and result oracle | GENERATED |
| `evidence/b-quick-validation.log` | B's five-fixture validation under the independent toolchain | GENERATED |
| `evidence/a-structural-console.log` | Human-readable 15-test result list | GENERATED |
| `evidence/structural-final/summary.json` | Machine-readable expected/actual results | GENERATED |
| `evidence/structural-final/fixtures/` | Exact generated positive and negative fixtures | GENERATED |
| `evidence/structural-final/*.log` | One complete Yosys log per fixture | GENERATED |
| `SHA256SUMS` | Integrity list for every other file in this package | GENERATED |

The package contains no modified production source and no official RTL copy.
