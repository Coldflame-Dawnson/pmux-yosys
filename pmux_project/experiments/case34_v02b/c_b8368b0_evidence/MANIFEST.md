# C b8368b0 evidence manifest

Candidate B: `b8368b0f3e2d55a2bb429296a3aea859eb4f2049`. Authorization A: `d03381f4511ab520372f3c2e57657b141a4959bc`. Verdict: G3 PASS.

Small tracked evidence: this directory and ../C_DEFAULT_PATH_FINAL_ACCEPTANCE_b8368b0.md.
Full raw evidence: `pmux_project/reports/c-b8368b0-default-final/` in the C worktree and delivery ZIP. Raw files: 151878; bytes: 765511594. RAW_INVENTORY.jsonl SHA-256: `41941f6d87e97767769b1d6dd95111c454c00bc926ebb650133cbb1bbfebe5fc`. Every raw file (including setup attempts, executable copies, build logs, native netlists, stats JSON, EQY configs and complete workdirs, 2000 seed rows and logs) has a content hash and size in that JSONL.

Main commands: `python3 build_c.py`, `python3 verify_frozen.py`, `python3 native_acceptance.py`, `python3 regress_c.py`, `python3 run_lifecycle.py`, `python3 run_core.py`, `python3 finalize_c.py`, run in this evidence directory in the exact B checkout before the C evidence commit. Use a new worktree/new empty output root to reproduce; drivers intentionally refuse existing final output directories. Build with fixed toolchain paths in frozen_provenance.json. Actual external commands are stored in commands.txt, raw per-run command.txt/command*.json and run.ys; seed wrappers also emit their actual arguments. Inspect per-run environment.json and toolchain.txt.

SHA256SUMS uses repository-relative paths. From repository root run `sha256sum -c pmux_project/experiments/case34_v02b/c_b8368b0_evidence/SHA256SUMS`. It includes all tracked C evidence and the complete raw inventory, selected official gold/default gates, final nets and whole-design EQY PASS markers. Use RAW_INVENTORY.jsonl to verify every remaining raw file. The checksum file excludes itself to avoid circular hashes; its Git object is bound by the pushed C commit. Delivery ZIP and commit hashes are recorded externally in DELIVERY_VERIFICATION.json.

Lifecycle observation fields in JSON are mode, authorization, payload_equal, lifecycle_valid, lifecycle_monitor_match. Profiles and reasons refer to actual consumer logs; pre_effect_four_field_assertions confirms real techmap/ABC9 boundary checks.

Preparation-stage failures are explicitly detailed in the final report and preserved in lifecycle_setup_attempt_* directories; they are excluded from final 58/58. A temporary protected-API compile error lacks a separate saved log, explicitly disclosed; it was not a required gate run. No candidate failure is hidden.
