# A unified-default independent audit

This directory contains A-side fixtures and drivers for the exact B commit
`dbb21ecd5d50d5636aec7415d9a8e6adb9b5f300`.

The drivers derive from the previously authorized independent width audit, but
all evidence is regenerated against the new default dispatcher. No prior gate,
random result, or B-generated acceptance result is reused.

The authoritative result is in `AUDIT_REPORT.md`. Reproduction commands are in
`commands.txt`, tool identities are in `toolchain.txt`, and compact evidence is
under `evidence/`. The large temporary work directories were intentionally not
committed; their machine-readable summaries, complete console logs, official
stage artifacts, EQY configurations/logs/PASS markers, and hashes are retained.
