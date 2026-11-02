# eLinx acceptance boundary and procedure

The installed application detected on the release machine is eLinx 3.0.7.
eLinx is the final vendor-side compile/simulation acceptance environment; it
does not replace Yosys resource counting or EQY formal equivalence.

No eLinx/FPGA/local-application control plugin is available. In accordance
with the operator's instruction, no desktop computer-control automation is
used. Therefore the current eLinx status is **NOT_EXECUTED**, not PASS.

## Required manual acceptance

1. Create or open the competition eLinx project with the official target
   device and constraints. Do not reuse an arbitrary teaching-demo device.
2. Add the official RTL and the competition-supplied testbench/constraints.
3. Run compile and simulation using the competition-prescribed flow.
4. Save the project file, compile log, simulation transcript and screenshots.
5. Record eLinx version, device, top module, return status and artifact hashes.

The built-in Tcl console documented by eLinx supports commands including:

```tcl
open_project <absolute-project.epr>
run_synthesis synth_1
run_implementation imple_1
generate_bitstream imple_1
close_project
```

Exact run names and the bitstream step must match the actual competition
project. A compile without the official project/device/testbench is not an
acceptance result.

## Evidence to retain

- `elinx-version.txt`
- project `.epr` and constraints
- synthesis/implementation/simulation logs
- testbench result and waveform or transcript
- SHA-256 of all inputs and generated reports
- a short `ELINX_RESULT.md` stating PASS/FAIL and any warnings
