# A audit: v0.2 documentation closure and final state

## 1. Decisions

| Scope | Decision | Exact boundary |
|---|---|---|
| B documentation commit | **AUTHORIZED** | Only `cb1e8199d35c894d0400a0bf4eef2aa626616026` |
| Production technical gate | **PASS** | Reuses the completed independent acceptance of production object `dbb21ecd5d50d5636aec7415d9a8e6adb9b5f300` recorded by A `feb137e459a329e01ff3ee87d3e3acb04a942d72`; no long test was rerun in this documentation-only audit |
| Complete historical P2 | **UNVERIFIED** | case4's historical 26 post-ABC9 `$lut` claim lacks the correct winner gate, executed configuration, PASS and hash closure; the known recreated table remains `REFUTED` |
| G3 technical gate | **PASS** | The exact production object satisfies the seven technical requirements and the two-person independence additions; this does not close the historical 26 claim |
| External competition acceptance of explicit primitives | **UNVERIFIED_EXTERNAL_SCORING** | Project EQY cannot establish organizer acceptance of `cycloneiv_lcell_comb` as a scored implementation choice |
| Merge/release | **NOT_EXECUTED** | This audit performs no main/integration merge, tag, release build or eLinx acceptance |

Authorization of the document commit does not transfer production authorization
to arbitrary descendants. The production evidence remains bound to `dbb21ec…`.

## 2. Git and integrity gate

- Remote branch `feature/b-v02-documentation-closure` independently resolved to
  `cb1e8199d35c894d0400a0bf4eef2aa626616026`.
- `HEAD^` is exactly `dbb21ecd5d50d5636aec7415d9a8e6adb9b5f300`.
- The commit adds exactly ten files: nine Markdown files and
  `design/DOCUMENT_SHA256SUMS`.
- Every new path is under `design/` and every mode is ordinary `100644`.
- The non-`design/` diff is empty; `.gitmodules`, submodule pointers, executable
  modes, production code, interfaces, tests and oracles are unchanged.
- `git diff --check` is clean and all files use LF line endings.
- `DOCUMENT_SHA256SUMS`: 9/9 Markdown entries match.
- Previous A evidence: 47/47 checksum entries match; the directory contains 48
  files including the checksum file itself.

Therefore the previous production test results may be cited as evidence for the
unchanged parent object. This round did not claim to rerun 19 tests, 1000+1000
seeds, Analyzer slow, official synthesis or EQY.

## 3. Contract review

### 3.1 Unified structural algorithm

Maintained. Current-state sections and the two-person contract prohibit runtime
dispatch by case, module, file, signal, automatic cell name, source location,
input hash or fixed vector. Case labels and expected values are restricted to
configuration, reporting and the acceptance oracle.

### 3.2 P0 > P1 > P2 > P3

Maintained without reordering. The documents explicitly distinguish:

- independently validated production values;
- historical experimental goals;
- missing/unverified evidence;
- refuted recreated candidates;
- the absence of a global optimality proof.

The current status block explicitly overrides stale `PENDING_A_AUDIT` and
`MISSING` statements retained in sections 0–12 as historical text. The archived
historical body is byte-for-byte identical to the current document's historical
body, so closure did not delete old objections or P0–P3 constraints.

### 3.3 Four-branch priority rule

The current v0.2 status explicitly requires the genuine outermost four-branch
priority chain, rejects taking an internal four-layer subchain from a
five-branch chain, permits variable data width, and does not authorize arbitrary
chain length. Overlapping selections retain priority semantics rather than an
assumed mutual-exclusion property.

Non-blocking wording issue: the G3 table says “任意合法宽度的优先链识别”. Read
with the current v0.2 status this means variable data width, but in isolation it
could be mistaken for arbitrary chain length. A future documentation-only patch
should replace it with “真正最外层四分支链；数据宽度可变；五分支及内部四层拒绝”.
The controlling v0.2 text already removes the substantive ambiguity, so this is
not a production or authorization blocker.

### 3.4 Existing proof obligations

No relaxation was found. The current sections retain:

- comparator parameter versus actual-port width checks and whole-batch reject;
- full fanout enumeration or explicit preservation;
- ordered support binding and relation multiplicity;
- cell/bit/event budgets and conservative UNKNOWN handling;
- immutable snapshots, atomic write planning and zero-write failure;
- monitor generation, content/target binding and one-shot consumption;
- no cross-DFF propagation, no local-to-global fact promotion, and conservative
  handling of `x/z`, signedness, width ambiguity and conflicts.

### 3.5 Pass versus offline validation

The documents correctly separate production Pass proof/snapshot/atomic rewrite
from offline full mapping, resource comparison, whole-design EQY and version
acceptance. They do not claim that the production Pass invokes whole-design EQY
for every candidate, implements an unrestricted global search, or proves a
global LUT optimum.

### 3.6 Role and authorization boundaries

Maintained. B cannot self-issue `AUTHORIZED` or G3, A does not repair B's
production implementation, and approval is bound to a complete commit hash.
Descendants, cherry-picks with different content and dirty worktrees do not
inherit approval.

### 3.7 Oracle and expected results

No oracle modification was found. The current tables reproduce A's prior
independent measurements and keep unlike metrics separate:

| case | post-ABC9 `$lut` | same-stage `cycloneiv_lcell_comb` | DFF | map_cells | profile | EQY |
|---|---:|---:|---:|---|---|---|
| 1 | 182 | 0 | 222 | 180 primitives + 2 `$not` | stock | PASS |
| 2 | 0 | 15 | 0 | 15 primitives | stock | PASS |
| 3 | 11 | 0 | 12 | 10 primitives + 1 `$not` | stock | PASS |
| 4 | 27 | 0 | 10 | 26 primitives + 1 `$not` | accepted_constant_table | PASS |

IO-cell delta is zero. Port-bit counts are not IO-cell counts. Case2's 10 LUT6
is reference-only; 15 vendor primitives are not 15 `$lut`. Case4's 26 vendor
primitives plus one `$not` are not evidence for 26 post-ABC9 `$lut`.

## 4. P2 determination

The evidence taxonomy makes two statements simultaneously true:

1. **27 is the best independently validated production result currently
   available for case4.** It is sufficient for the already completed production
   technical acceptance and the G3 seven-condition improvement gate.
2. **The historical 26 claim has not been closed.** Its correct winning gate,
   executed EQY configuration, PASS marker and hash closure remain absent. The
   incorrect recreated table proves only that one reconstruction is invalid; it
   does not prove that every possible 26-LUT candidate is impossible.

Consequently, complete historical P2 is `UNVERIFIED`, not `PASS` and not
`REFUTED`. This does not silently lower 26 to 27.

The project owner must make one explicit release-scope decision:

- **Verified-production scope:** release the independently validated 27-LUT
  production result while retaining 26 as a research/evidence backlog. This is
  compatible with current technical G3 PASS.
- **Historical-target scope:** require case4 26 post-ABC9 `$lut` before release.
  Under that scope release remains blocked until the correct gate, configuration,
  log/PASS and hashes are recovered or regenerated.

Neither choice permits using the refuted table or the 26-primitives-plus-`$not`
mapping as a substitute.

## 5. G3, scoring and release

For exact production commit `dbb21ec…`, the existing A evidence establishes:
microtests pass; both 1000-seed sets pass EQY; all four official whole-design
EQY jobs pass; LUT does not regress and multiple cases improve; DFF/IO do not
increase; CPU/ABC/RSS are recorded; and identity dispatch is absent. B authored
production and A independently regenerated and accepted it. Therefore the G3
technical gate is `PASS` under the published two-person contract.

This G3 result is separate from:

- complete historical P2: `UNVERIFIED`;
- organizer acceptance of explicit vendor primitives:
  `UNVERIFIED_EXTERNAL_SCORING`;
- eLinx acceptance: not run for this final object;
- mechanical merge and release: not executed.

## 6. Evidence portability and historical links

The disclosed historical-external-reference count of 309 was independently
reproduced using the documented scope: current v0.2 historical body, the archive
interpreted against the old `design/` base, engineering status, and delivery
status. A literal Markdown resolver reports 350 missing relative-link
occurrences because archive links naturally resolve under `design/archive/` and
because the historical body is intentionally duplicated. These are historical
attachment references, not current executable dependencies, and the documents
do not claim they are all present in the central repository.

The previous A `commands.txt` contains the absolute paths actually used during
the audit. It is valid provenance, but not a copy-paste portable command file.
The new status document correctly directs a fresh recipient to the Git objects,
parameterized runners, local trusted RTL plus frozen hashes, and a locally
selected tool/output path. A future packaging improvement may add a portable
wrapper without modifying the checksum-closed historical evidence.

## 7. Mechanical integration authorization

This documentation commit may be used as the source for a later mechanical
integration, subject to all of the following:

1. Dawnson/project owner names the exact destination branch and release scope.
2. The integration contains production tree `dbb21ec…` plus these ten document
   files and no unreviewed production/oracle/interface changes.
3. A checks the final integration commit's parentage/tree/diff and document
   hashes; only a short diff/integrity check is needed if content remains exact.
4. The final status continues to label full historical P2 and external scoring
   accurately; a release claiming historical P2 completion requires the missing
   26-LUT evidence first.

No integration target was selected and no merge was performed in this task.

## 8. Final statement

`AUTHORIZED` for documentation commit
`cb1e8199d35c894d0400a0bf4eef2aa626616026`.

Production technical G3 remains `PASS` for exact object
`dbb21ecd5d50d5636aec7415d9a8e6adb9b5f300`; complete historical P2 remains
`UNVERIFIED`; external explicit-primitive scoring remains
`UNVERIFIED_EXTERNAL_SCORING`; merge and release remain `NOT_EXECUTED`.
