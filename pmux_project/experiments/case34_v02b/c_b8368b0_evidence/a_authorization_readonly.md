# A re-audit: generation and registered lifecycle-monitor binding

## Verdict

`AUTHORIZED`

B commit `b8368b0f3e2d55a2bb429296a3aea859eb4f2049` binds the
constant-table generation token to an actual `LifecycleMonitor` registered in
the current `RTLIL::Design`.  A independently reproduced the source-loaded
default `synth_intel` path and the `pmux_opt_abc9` runner path.  A forged or
replayed generation is rejected by both paths before mapping side effects,
while the one legal first consumption remains accepted.  C may begin final
default-path validation of this exact B commit.

## Audited objects and scope

- B branch: `feature/b-v02-unified-selection-gate`
- audited B commit: `b8368b0f3e2d55a2bb429296a3aea859eb4f2049`
- exact parent: `ec86209a61f22020707b299f5175f02b8d71495d`
- previous A content: `18c7995aba4d6337a8d962fe2b700e2584ea13d2`
- previous A final HEAD: `600548ba9b5016acd9870c26c0e6522397145596`
- A branch: `feature/a-v02-generation-monitor-reaudit-b8368b0`
- fixed toolchain:
  `/home/james/work/pmux-yosys/pmux_project/official_baseline/toolchain/install`

The audited commit is the direct child requested.  Its diff is limited to the
shared authorization helper, both consumers, B tests, README and status
documentation.  `pmux_project/include/` has no diff, so frozen proof APIs and
proof obligations are unchanged.  A added only files and targets under
`pmux_project/analyzer/`; it did not modify production code, `techlibs/intel/`,
official RTL, or test expectations.

The production diff contains no case/file/top/module/signal/automatic-cell-name
or fixed-vector dispatch.  Module and cell names occur only in the post-proof
snapshot as immutable object identity; they do not discover a candidate or
enable a rule.

## Lifecycle contract results

| # | Obligation | Result | Evidence |
|---:|---|---|---|
| 1 | legal first publish/consume | PASS | both paths select `accepted_constant_table` |
| 2 | arbitrary nonempty forged generation | PASS | monitor match 0, stock, lifecycle mismatch |
| 3 | empty generation | PASS | stock |
| 4 | lifecycle false | PASS | stock |
| 5 | monitor absent/unregistered | PASS | A removes the actual registration; both paths select stock |
| 6 | old monitor generation | PASS | two distinct generations are produced; replay of the first has monitor match 0 |
| 7 | generation from another design | PASS | lookup iterates only current-design monitors and also checks `monitor->design == design`; arbitrary foreign token test rejects |
| 8 | consecutive publications in one design | PASS | publication removes the owned previous monitor before registering the next generation |
| 9 | delete and same-name/same-content rebuild | PASS | equal payload, lifecycle false, stock on both paths |
| 10 | port change followed by exact restore | PASS | equal payload, lifecycle remains false, stock |
| 11 | module delete and same-name/full-content rebuild | PASS | equal payload, monitor match 0, stock on both paths |
| 12 | allocator-address reuse cannot restore authorization | PASS | invalidation is a scratchpad generation property and is irreversible by pointer identity; 256 real delete/recreate attempts per path remained rejected |
| 13 | content snapshot remains complete | PASS | B 14/14 mutation matrix covers type, parameter, attribute, port, bit order, target set/count and replay |
| 14 | clear all four transient fields before effects | PASS | every A row asserts authorization=false, snapshot empty, generation empty and lifecycle=false after consumption |
| 15 | runner/native conclusion and reason agreement | PASS | identical accepted/rejected profiles and `target_lifecycle_mismatch` reasons |
| 16 | cross-DSO `dynamic_cast` reaches the actual monitor | PASS | legal source-loaded native path reports monitor match 1 and accepts; forged path reports 0 and rejects |
| 17 | monitor ownership and repeated-use safety | PASS | owner erases registration before `unique_ptr` release; normal success/failure and repeated publication/consumption leave no active authorization |

No obligation is `FAIL` or `UNVERIFIED` for the authorization decision.

## Shared helper and consumer ordering

`registered_lifecycle_monitor()` first reads the current generation, rejects an
empty token, iterates `design->monitors`, performs a real cross-DSO
`dynamic_cast<LifecycleMonitor *>`, and requires both the current design pointer
and immutable monitor generation to match.  `constant_table_lifecycle_matches()`
additionally requires authorization=true, lifecycle=true and `monitor->active()`.
`constant_table_authorization_matches()` then compares the complete nonempty
target snapshot.

Both consumers perform this order:

1. read the transient authorization state;
2. compute lifecycle and complete authorization matches against the current design;
3. save the selected profile in a local Boolean;
4. unconditionally clear authorization, snapshot, generation and lifecycle;
5. only then execute techmap/ABC9 side effects.

The runner and source-loaded native consumer call the same inline helper.  The
ELF objects expose compatible `LifecycleMonitor` RTTI, and the real legal and
forged rows prove that the DSO boundary is operational rather than merely a
same-named-type source check.

`RTLIL::Design::~Design()` does not own or delete monitors.  Production ownership
is instead held by the helper registry.  Normal publish/consume and a subsequent
publication call `remove_owned_lifecycle_monitors()`, erase each raw pointer from
`Design::monitors`, and only then release its `unique_ptr`.  A destroyed design
cannot expose its old monitor through a new design's monitor set; deleting the
owner later does not dereference the stored design pointer.  Repeated legal
publish/consume therefore does not accumulate active state or leave a dangling
registered monitor.

## Independent two-path mutation matrix

Command:

```bash
make -C pmux_project/analyzer profile-snapshot-reaudit \
  YOSYS_ROOT=/home/james/work/pmux-yosys/pmux_project/official_baseline/toolchain/install \
  OFFICIAL_ROOT=/home/james/work/pmux-yosys/pmux_project/pmux_case/competition_case \
  PROFILE_SNAPSHOT_AUDIT_OUT=/tmp/a-generation-monitor-matrix
```

Result: `18/18 PASS` (nine modes on each of two real consumers).

| Mutation | native `synth_intel` | runner |
|---|---|---|
| legal first consume | accepted_constant_table | accepted_constant_table |
| same-name cell rebuild | stock | stock |
| same-name module rebuild | stock | stock |
| port change then restore | stock | stock |
| lifecycle=false | stock | stock |
| empty generation | stock | stock |
| arbitrary forged generation | stock | stock |
| previous generation replay after a second publish | stock | stock |
| actual monitor registration removed | stock | stock |

Core forged-generation observations on both paths:

```text
lifecycle_valid=1
lifecycle_monitor_match=0
profile=stock
reason=target_lifecycle_mismatch
```

Old-generation replay produced distinct `old` and `new` tokens, then the exact
same observation.  Cell and module rebuild rows retained an equal content
payload but lifecycle match remained false.  All rows verified four-field
clearing after consumption.

Evidence:

- `/tmp/a-generation-monitor-matrix/summary.json`
- `/tmp/a-generation-monitor-matrix/logs/`
- `pmux_project/analyzer/profile_snapshot_reaudit.log`

## B matrix and regressions

B's real snapshot/lifecycle matrix was rebuilt and rerun independently:

```bash
python3 pmux_project/experiments/case34_v02b/scripts/run_profile_snapshot_tests.py \
  --tool-root /home/james/work/pmux-yosys/pmux_project/official_baseline/toolchain/install \
  --official-dir /home/james/work/pmux-yosys/pmux_project/pmux_case/competition_case \
  --output /tmp/b-generation-monitor-matrix
```

Result: `14/14 PASS`.

Other results:

- production decision mutation seam: `26/26 PASS`;
- A decision-consumer mutations: `13/13 PASS`;
- A ordered-structure fixtures: `12/12 PASS`;
- analyzer fast: `2116 / 29 / 60 / 50 / 81 PASS`;
- analyzer slow: `2118 / 29 / 60 / 50 / 81 PASS`;
- real micro suite: `19/19 PASS`, with structure/check/EQY/semantic all PASS;
- case3 decision-cone report: proved, `24 cells / 50 bits / 216390 events`;
- case3 production proof: proved, `12 cells / 25 bits / 195180 events`;
- case4 production proof: proved, `23 cells / 122 bits / 7784 events`;
- all three read-only reports: RTLIL fingerprint unchanged.

The exact regression commands were:

```bash
make -C pmux_project/analyzer test YOSYS_ROOT=<fixed-tool-root>
make -C pmux_project/analyzer decision-consumer-reaudit YOSYS_ROOT=<fixed-tool-root>
make -C pmux_project/analyzer test-slow YOSYS_ROOT=<fixed-tool-root>
make -C pmux_project/analyzer decision-cone-report production-case34-report \
  YOSYS_ROOT=<fixed-tool-root>

<yosys> -m pmux_project/pass/pmux_opt.so -p \
  'read_verilog test3.v; synth_intel -family cycloneiv -top test3 \
   -run begin:map_ffram; opt -fast -mux_undef -undriven -fine -full; \
   memory_map; opt -undriven -fine; pmux_opt -test-decision-proof-mutations'

python3 pmux_project/scripts/run_micro.py --mode external \
  --external-kind real --bin <fixed-tool-bin> --out /tmp/a-generation-micro-19 \
  --dut-template 'python3 {project}/scripts/real_pass_adapter.py ...'
```

Evidence logs:

- `/tmp/a-generation-production-mutations.log`
- `/tmp/a-generation-analyzer-fast.log`
- `/tmp/a-generation-decision-reaudit.log`
- `/tmp/a-generation-analyzer-slow.log`
- `/tmp/a-generation-readonly-reports.log`
- `/tmp/a-generation-micro-19/`

## Four official cases

Command:

```bash
python3 pmux_project/experiments/case34_v02b/scripts/run_production_validation.py \
  --repo /home/james/work/pmux-yosys-a-generation-monitor-reaudit \
  --tool-bin /home/james/work/pmux-yosys/pmux_project/official_baseline/toolchain/install/bin \
  --official /home/james/work/pmux-yosys/pmux_project/pmux_case/competition_case \
  --out /tmp/a-generation-production-validation
```

| case | baseline LUT/DFF | default LUT/DFF | profile | whole-design EQY |
|---|---:|---:|---|---|
| case1 | 288/222 | 288/222 | stock | PASS |
| case2 | 25/0 | 25/0 | stock | PASS |
| case3 | 13/17 | 11/12 | stock | PASS |
| case4 | 36/10 | 27/10 | accepted_constant_table | PASS |

Evidence is under `/tmp/a-generation-production-validation/`, with the aggregate
record at `summary.json` and the profile selection in each
`case*/production/yosys.log`.

## Final authorization statement

The arbitrary-generation blocker reported against `ec86209a...` is fixed in
`b8368b0...`.  The generation must now identify a live, registered monitor for
the exact current design, and full content snapshot matching remains mandatory.
All rejection and success paths consume all four transient fields before mapping
side effects.  Therefore A authorizes C to start final default-path validation
of exactly `b8368b0f3e2d55a2bb429296a3aea859eb4f2049`.

Final decision: `AUTHORIZED`.
