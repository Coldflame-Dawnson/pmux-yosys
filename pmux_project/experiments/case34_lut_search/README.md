# Case3/case4 LUT search

This experiment searches pass ordering, ABC9 scripts, and structural rewrites
on official case3 and case4. The runners reproduce the official staged
`synth_intel -family cycloneiv` flow and count final `$lut` cells.

The current findings are recorded in `RESULTS.md`. They are experimental
opportunities, not production rules. A candidate must have a structural
precondition, atomic fallback, and EQY coverage before it can enter the Pass.

Main entry points:

- `search_pass_sequences.py`: existing Yosys pass-order matrix.
- `search_abc9_scripts.py`: ABC9 preprocessing and mapper matrix.
- `search_case4_splits.py`: one shared Shannon split variable for all outputs.
- `search_case4_mixed.py`: per-output split variables plus deterministic search.
