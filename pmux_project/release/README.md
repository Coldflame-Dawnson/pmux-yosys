# PMUX v0.2 G3 release

This directory is the reproducible release entry point for the verified G3
implementation. The production source is the repository tree; generated
plugins, official competition RTL and validation work directories are not
committed.

## Reproduce

From the repository root:

```bash
pmux_project/release/build_and_validate.sh \
  /absolute/path/to/oss-cad-suite \
  /absolute/path/to/competition_case \
  /absolute/path/to/output
```

The tool root must contain `bin/yosys`, `bin/yosys-config`, `bin/eqy` and
`bin/yosys-abc`. The official directory must contain `test1.v` through
`test4.v` with the hashes in `official-inputs.sha256`.

The command builds `pmux_opt.so`, runs the unified native and runner paths,
checks default/explicit/rollback behavior, and performs whole-design EQY for
all four official cases. Success requires `47/47 PASS`.

## Verified result

The landing validation on 2026-11-02 used G3 commit
`12ac8b86cfd82d1439ae47d4b7b198b26f0e545a` and passed 47/47 checks.
See `RELEASE_STATUS.md` for exact resource counts and remaining boundaries.

The historical case4 claim of 26 post-ABC9 `$lut` remains unverified. The
verified production result is 27 post-ABC9 `$lut`. eLinx compilation and
external scoring are separate acceptance steps and are not implied by EQY.
