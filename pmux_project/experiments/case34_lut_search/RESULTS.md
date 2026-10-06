# Official case3/case4 LUT search results

Date: 2026-10-06

## Conclusion

The best verified experimental results in this search are:

| Case | Official baseline | Best observed | Reduction | Relative reduction | Whole-design EQY |
|---|---:|---:|---:|---:|---|
| case3 | 13 LUT | 11 LUT | 2 LUT | 15.38% | PASS |
| case4 | 36 LUT | 26 LUT | 10 LUT | 27.78% | PASS |

These are the lowest values observed in the bounded searches below. They are
not mathematical proofs of minimum LUT count.

## Flow control

The search reads the official Verilog and reproduces the staged Cyclone IV
flow. The baseline runner reproduced 13 LUT for case3 and 36 LUT for case4.
This check matters because resuming directly from a serialized RTLIL snapshot
changed case4 from 36 to 35 LUT and therefore was not used for final claims.

## case3

### Observations

1. Sixteen combinations of existing Yosys passes were tested.
2. `pmuxtree` alone reduced case3 from 13 to 12 LUT.
3. Replacing only PMUX data bits was insufficient in the earlier production
   experiment because the select structure remained.
4. Under the outer guard `sr[7] == 1`, the three case comparisons can drop the
   already-known bit, and their selected data values can be constant-folded.
5. Applying both transformations together reduced case3 to 11 LUT. Further
   ABC9 script variants did not reduce it below 11.

### Candidate rule

Use guard facts to perform a joint rewrite:

1. prove input bits fixed by the enclosing guard;
2. remove those fixed bits from equality/inequality comparisons;
3. propagate the same facts into selected data expressions;
4. rebuild the affected decision structure;
5. run cleanup and accept only if the cost gate improves.

This rule is stronger than the existing data-bit-only rewrite because it can
delete or shrink selection logic as well as constantize branch data.

## case4

### Observations

1. Existing Yosys pass order changes did not reduce the 36-LUT baseline.
2. Replacing the default ABC9 choice generation with `&dch -e -r` reduced the
   original structure to 33 LUT.
3. case4 is a 5-input, 10-output constant lookup table after memory mapping.
4. A 4-LUT-oriented Shannon decomposition reduced it to 31 LUT with a fixed
   split, and enumeration of the five shared split variables reached 30 LUT.
5. Choosing the split variable independently for each output and retaining
   cross-output optimization reached 26 LUT. The winning vector, indexed by
   output bit 0 through 9, was:

   `[3, 3, 1, 4, 4, 4, 0, 2, 4, 4]`

6. Expanding the deterministic search from 120 to 500 mixed candidates and
   four greedy refinement rounds did not improve on 26 LUT.

The winning ABC9 body was:

```text
&scorr; &sweep; &dc2; &dch -e -r;
&if -a -W 300 -v; &mfs; &mfs
```

### Candidate rule

Detect a bounded constant mux/ROM cone and rebuild it for the target LUT width:

1. recover address variables and output truth tables;
2. enumerate every legal Shannon split variable per output;
3. form 4-input cofactors and remove constant or duplicate cofactors;
4. search mixed per-output split vectors while preserving shared logic;
5. use a cheap structural estimate for broad search and actual ABC9 LUT count
   only for finalists;
6. commit the rewrite atomically only after EQY and a positive cost result.

## Proposed production priority

1. Constant-table/mux-cone cofactor decomposition for the target LUT width.
2. Guard-conditioned comparison shrinking plus branch constantization.
3. Existing PMUX data-bit rewrite as a fallback.
4. Existing no-change path when proof, budget, or cost checks fail.

The ABC9 area-oriented script is a flow-level candidate, not a proof rule. It
must remain separately configurable so the structural Pass does not silently
depend on one mapper script.

## Verification boundary

- The 11-LUT case3 netlist passed whole-design EQY against official `test3.v`.
- The 26-LUT case4 netlist passed whole-design EQY against official `test4.v`.
- DFF semantics, including case4's intentionally unassigned
  `datain_5bis7` reset behavior, were preserved by EQY.
- Production Pass implementation, atomic fallback tests, the 19 microcases,
  and the 1000-seed regression have not yet been rerun for these new rules.
