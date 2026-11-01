# B mechanical integration evidence manifest

Object under review: `12ac8b86cfd82d1439ae47d4b7b198b26f0e545a`.
Source: `cb1e8199d35c894d0400a0bf4eef2aa626616026`.
Frozen target: `f78cb2fd1894c2e1f7197d121a4d73db980c85a7`.

This directory is published ONLY on the B evidence branch
`feature/b-g3-mechanical-evidence-12ac8b8`. It is absent from the mechanical
candidate, whose entire tree must remain identical to the authorized source.
Do not use the evidence branch as the integration source.

| File | Purpose |
|---|---|
| `DELIVERY.md` | Human report, exact parents/tree, inherited-whitespace disclosure, status boundaries |
| `A_REVIEW_PROMPT.md` | Independent A final review instructions |
| `RESULTS.json` | Machine assertions; remote push verification is reported separately |
| `commands.json` | Exact command argv, cwd and return codes, in execution order |
| `raw-command-output.tar.gz` | Unmodified `NN.stdout`/`NN.stderr` from each recorded command; empty files are intentional; internal `SHA256SUMS` covers every raw member |
| `run_mechanical_integration_cb1e819.py` | Initial guarded merge driver; stopped at inherited whitespace rc=2 |
| `finalize_mechanical_integration_cb1e819.py` | Guarded continuation proving identical index/source and inherited warnings before commit |
| `SHA256SUMS` | Every other file in this directory, excluding itself |

The initial driver is one-shot and the continuation records an already-completed
operation. Their local paths document B's execution, not portable dependencies
that A must access. A should use the Git objects and independent commands in the
review prompt. No credentials, official RTL, compiled binaries, or new long-test
results are included.

Raw diff outputs are archived without whitespace cleanup so Git does not treat
quoted source/log whitespace as newly authored text. Extract the archive to a
scratch directory and run `sha256sum -c SHA256SUMS` there before examining outputs.

`git diff --cached --check` returned 2, with 99 inherited whitespace findings
across 11 original Markdown/log files. The original `17.stdout` equals the later
source-diff and staged-diff output byte for byte. There was no file cleanup or
manual conflict resolution. Source-to-final whole-tree diff is empty and its
diff-check succeeds. This distinction is material and must not be omitted.

The candidate's newly created worktree has uninitialized submodules; source and
candidate gitlinks are identical. No build or submodule initialization is claimed.
No long regression was rerun. Final A authorization remains pending, and the
integration target, release and eLinx have not been updated/executed by this work.
