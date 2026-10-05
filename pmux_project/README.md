# PMUX Optimization Project

This directory contains the team materials for the 2026 PMUX optimization task.

Authoritative documents:

- `docs/PMUX优化技术方案_v0.1.md`
- `docs/G3三人并行分工与接口契约.md`
- `docs/M0三人协作启动检查清单.md`

The repository is based on Yosys commit
`9f75ca1f9834a39a863915b5dae0c7b1e33533bc` (v0.69).

The four official Verilog inputs are intentionally not included yet. Restore
them from the trusted official package, verify the SHA-256 values recorded in
the M0 checklist, and place local copies under:

```text
pmux_project/pmux_case/competition_case/
```

Generated toolchains, builds, logs, netlists, EQY work directories, and
benchmark outputs must not be committed.
