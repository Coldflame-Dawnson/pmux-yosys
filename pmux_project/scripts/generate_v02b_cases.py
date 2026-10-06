#!/usr/bin/env python3
"""Construct V2-B branch-deletion verification fixtures, never a proof engine."""

import json

from run_micro import ROOT, save

OUT = ROOT / "tests" / "pmux_v02b" / "micro"


class Module:
    def __init__(self):
        self.wires = []
        self.cells = []
        self.connects = []
        self.port = 0

    def wire(self, name, width=1, direction=None):
        port = ""
        if direction:
            self.port += 1
            port = f" {direction} {self.port}"
        self.wires.append(f"  wire width {width}{port} \\{name}")
        return "\\" + name

    def cell(self, kind, name, params, ports, keep=False):
        lines = []
        if keep:
            lines.append("  attribute \\keep 1")
        lines.append("  cell $" + kind + f" \\{name}")
        lines.extend(f"    parameter \\{key} {value}" for key, value in params.items())
        lines.extend(f"    connect \\{key} {value}" for key, value in ports.items())
        lines.append("  end")
        self.cells.append("\n".join(lines))

    def render(self):
        return "\n".join(["module \\top", *self.wires, *self.cells, *self.connects, "end", ""])


def concat(parts):
    return parts[0] if len(parts) == 1 else "{ " + " ".join(reversed(parts)) + " }"


def eq_selects(module, count):
    ctrl = module.wire("ctrl", 3, "input")
    result = []
    for i in range(count):
        name = module.wire(f"s{i}")
        module.cell("eq", f"cmp{i}",
                    {"A_SIGNED": 0, "B_SIGNED": 0, "A_WIDTH": 3,
                     "B_WIDTH": 3, "Y_WIDTH": 1},
                    {"A": ctrl, "B": f"3'{i:03b}", "Y": name})
        result.append(name)
    return result


def build(name, count, removed=(), width=1, selector="exclusive",
          equal_a=(), fallback=False, partial=False, keep=False,
          extra_cell=False, reason="fixture_branch_delete", budget=None):
    module = Module()
    if selector == "exclusive":
        selects = eq_selects(module, count)
    else:
        sel = module.wire("sel", count, "input")
        selects = [f"{sel} [{i}]" for i in range(count)]
        if selector == "fallback":
            or_out = module.wire("fallback_s")
            module.cell("or", "fallback_or",
                        {"A_SIGNED": 0, "B_SIGNED": 0,
                         "A_WIDTH": 1, "B_WIDTH": 1, "Y_WIDTH": 1},
                        {"A": selects[0], "B": selects[1], "Y": or_out})
            selects[1] = or_out
        if selector == "conflict":
            neg = module.wire("not_s")
            conflict = module.wire("conflict_s")
            module.cell("not", "not_sel",
                        {"A_SIGNED": 0, "A_WIDTH": 1, "Y_WIDTH": 1},
                        {"A": selects[0], "Y": neg})
            module.cell("and", "conflict_and",
                        {"A_SIGNED": 0, "B_SIGNED": 0,
                         "A_WIDTH": 1, "B_WIDTH": 1, "Y_WIDTH": 1},
                        {"A": selects[0], "B": neg, "Y": conflict})
            selects[0] = conflict
        if selector == "unknown":
            xor_out = module.wire("xor_s")
            module.cell("xor", "unknown_xor",
                        {"A_SIGNED": 0, "B_SIGNED": 0,
                         "A_WIDTH": 1, "B_WIDTH": 1, "Y_WIDTH": 1},
                        {"A": selects[0], "B": selects[1], "Y": xor_out})
            selects[0] = xor_out
    a = module.wire("a", width, "input")
    data = [module.wire(f"d{i}", width, "input") for i in range(count)]
    b = [a if i in equal_a else data[i] for i in range(count)]
    if fallback:
        b[0] = data[1]
    if partial:
        b[0] = "{ \\d0 [1] \\a [0] }"
    y = module.wire("out", width, "output")
    module.cell("pmux", "target", {"WIDTH": width, "S_WIDTH": count},
                {"A": a, "B": concat(b), "S": concat(selects), "Y": y},
                keep=keep)
    if extra_cell:
        y2 = module.wire("other_out", width, "output")
        module.cell("pmux", "other", {"WIDTH": width, "S_WIDTH": count},
                    {"A": a, "B": concat(b), "S": concat(selects), "Y": y2})
    before = module.render()
    if removed:
        module.cells = [cell for cell in module.cells if " \\target\n" not in cell]
        remaining = [i for i in range(count) if i not in removed]
        if len(remaining) >= 2:
            module.cell("pmux", "target",
                        {"WIDTH": width, "S_WIDTH": len(remaining)},
                        {"A": a, "B": concat([b[i] for i in remaining]),
                         "S": concat([selects[i] for i in remaining]), "Y": y})
            result_type = "$pmux"
        elif len(remaining) == 1:
            i = remaining[0]
            module.cell("mux", "target", {"WIDTH": width},
                        {"A": a, "B": b[i], "S": selects[i], "Y": y})
            result_type = "$mux"
        else:
            module.connects.append(f"  connect {y} {a}")
            result_type = "DELETED"
        after = module.render()
    else:
        after = before
        result_type = "$pmux"
    expected = {
        "id": name, "top": "top", "before": name + ".before.il",
        "after": name + ".after.il", "target": "target",
        "branches_before": count, "removed_branches": list(removed),
        "branches_after": count - len(removed),
        "target_after_type": result_type,
        "reason_category": reason, "mode": "FIXTURE",
        "budget": budget, "selection": ["target"] if extra_cell else None,
        "expect_atomic_zero_write": reason in (
            "fixture_snapshot_mismatch", "fixture_invalid_branch_index"),
        "select_relation": selector,
    }
    return before, after, expected


def make():
    return [
        build("01_first", 3, (0,), equal_a=(0,)),
        build("02_middle", 3, (1,), equal_a=(1,)),
        build("03_last", 3, (2,), equal_a=(2,)),
        build("04_more_than_two_left", 4, (1,), equal_a=(1,)),
        build("05_to_mux", 2, (0,), equal_a=(0,)),
        build("06_last_to_a", 1, (0,), equal_a=(0,)),
        build("07_multi_delete", 4, (0, 2), equal_a=(0, 2)),
        build("08_equal_default_a", 3, (1,), equal_a=(1,)),
        build("09_equal_lower_fallback", 2, selector="fallback",
              fallback=True, reason="fixture_non_a_fallback_reject"),
        build("10_exclusive_select", 3, (0,), equal_a=(0,)),
        build("11_possible_overlap", 2, selector="overlap", equal_a=(1,),
              reason="fixture_select_overlap"),
        build("12_partial_bits_reject", 2, width=2, selector="overlap",
              partial=True, reason="fixture_partial_bits"),
        build("13_proof_unknown", 2, selector="unknown",
              reason="fixture_proof_unknown"),
        build("14_condition_conflict", 2, selector="conflict",
              reason="fixture_condition_conflict"),
        build("15a_depth_budget", 2, selector="overlap",
              reason="fixture_budget_depth", budget={"kind": "depth", "forced": True}),
        build("15b_cell_budget", 2, selector="overlap",
              reason="fixture_budget_cells", budget={"kind": "cells", "forced": True}),
        build("15c_bit_budget", 2, selector="overlap",
              reason="fixture_budget_bits", budget={"kind": "bits", "forced": True}),
        build("15d_event_budget", 2, selector="overlap",
              reason="fixture_budget_events", budget={"kind": "events", "forced": True}),
        build("16_keep", 2, selector="exclusive", equal_a=(0,), keep=True,
              reason="fixture_keep"),
        build("17_selection_partial", 3, (0,), equal_a=(0,), extra_cell=True,
              reason="fixture_selection_scope"),
        build("18_snapshot_mismatch", 2, selector="exclusive", equal_a=(0,),
              reason="fixture_snapshot_mismatch"),
        build("19_invalid_branch", 2, selector="exclusive", equal_a=(0,),
              reason="fixture_invalid_branch_index"),
    ]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    for before, after, spec in make():
        save(OUT / spec["before"], before)
        save(OUT / spec["after"], after)
        rows.append(spec)
    save(OUT / "expectations.json",
         json.dumps({"schema": 1, "mode": "FIXTURE",
                     "cases": rows}, ensure_ascii=False, indent=2) + "\n")
    print(f"generated {len(rows)} branch fixtures; case3 positive is supplied separately")


if __name__ == "__main__":
    main()
