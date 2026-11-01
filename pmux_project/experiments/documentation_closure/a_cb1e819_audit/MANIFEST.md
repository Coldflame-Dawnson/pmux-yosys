# Manifest

| Path | Purpose |
|---|---|
| `AUDIT_REPORT.md` | Human-readable scope decisions and reasoning |
| `results.json` | Machine-readable final states |
| `run_doc_audit.sh` | Parameterized, short independent integrity audit |
| `commands.txt` | Exact invocation and verification commands |
| `evidence/remote.txt` | Independent remote-ref observation |
| `evidence/git-scope.txt` | HEAD, parent, trees, changed paths, modes and status |
| `evidence/a-working-tree.diff`, `evidence/a-index.diff` | Empty proof that A did not alter tracked B content before its own commit |
| `evidence/non-design.diff` | Empty proof of zero non-design diff |
| `evidence/diff-check.txt` | Empty proof of clean patch formatting |
| `evidence/document-sha256-check.txt` | 9/9 document checksum verification |
| `evidence/prior-a-sha256-check.txt` | 47/47 previous A evidence verification |
| `evidence/document-structure.json` | File, LF and historical-link accounting |
| `evidence/RESULT` | Short audit script terminal state |
| `SHA256SUMS` | Hash closure for every audit file except itself |

No production source, interface, official RTL, oracle or previous A evidence is
copied or modified here.
