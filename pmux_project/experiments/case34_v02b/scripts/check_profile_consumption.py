#!/usr/bin/env python3
"""Guard snapshot-bound, one-shot profile authorization in both paths."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
SCRATCHPAD = 'pmux_opt.accepted_constant_table'
SNAPSHOT = 'pmux_opt.accepted_constant_table_snapshot'


def require_order(path: Path, start_marker: str, get_marker: str,
                  clear_marker: str, first_effect_marker: str) -> None:
    text = path.read_text(encoding="utf-8")
    start = text.index(start_marker)
    get = text.index(get_marker, start)
    clear = text.index(clear_marker, get)
    first_effect = text.index(first_effect_marker, get)
    if not start < get < clear < first_effect:
        raise AssertionError(f"authorization is not consumed before effects in {path}")


def consume(authorized: bool, expected: str, actual: str, cell_count: int,
            generation: str, lifecycle_valid: bool,
            registered_generation: str = "generation-a") -> tuple[str, bool, str, str, bool]:
    matched = (authorized and bool(expected) and cell_count > 0
               and bool(generation) and lifecycle_valid
               and generation == registered_generation and expected == actual)
    profile = "accepted_constant_table" if matched else "stock"
    return profile, False, "", "", False


def main() -> int:
    clear = 'clear_constant_table_authorization'
    get = 'constant_table_authorization_matches'
    require_order(
        ROOT / "techlibs/intel/synth_intel.cc",
        'if (check_label("map_ffram"))', get, clear,
        'run("techmap -map +/techmap.v")',
    )
    require_order(
        ROOT / "pmux_project/pass/pmux_opt.cc",
        'struct PmuxOptAbc9Pass', get, clear,
        'Pass::call(design, stringf("techmap -map %s"',
    )

    helper = (ROOT / "pmux_project/pass/pmux_profile_authorization.h").read_text(encoding="utf-8")
    assert SCRATCHPAD in helper and SNAPSHOT in helper
    assert "build_constant_table_snapshot" in helper
    assert "actual.payload == expected" in helper
    assert "cell->parameters" in helper and "cell->attributes" in helper
    assert "cell->connections()" in helper and "cell->type" in helper
    assert "LifecycleMonitor" in helper and "notify_connect" in helper
    assert "notify_module_del" in helper and "generation_key" in helper
    assert "lifecycle_key" in helper
    assert "registered_lifecycle_monitor" in helper
    assert "dynamic_cast<LifecycleMonitor *>" in helper
    assert "monitor->generation == generation" in helper

    profile, authorization, snapshot, generation, lifecycle = consume(
        True, "snapshot-a", "snapshot-a", 3, "generation-a", True
    )
    assert profile == "accepted_constant_table"
    assert not authorization and not snapshot and not generation and not lifecycle
    replay = consume(authorization, snapshot, "snapshot-a", 3, generation, lifecycle)
    assert replay[0] == "stock"
    assert consume(False, "snapshot-a", "snapshot-a", 3, "generation-a", True)[0] == "stock"
    assert consume(True, "", "snapshot-a", 3, "generation-a", True)[0] == "stock"
    assert consume(True, "snapshot-a", "snapshot-b", 3, "generation-a", True)[0] == "stock"
    assert consume(True, "snapshot-a", "snapshot-a", 0, "generation-a", True)[0] == "stock"
    assert consume(True, "snapshot-a", "snapshot-a", 3, "", True)[0] == "stock"
    assert consume(True, "snapshot-a", "snapshot-a", 3, "generation-a", False)[0] == "stock"
    assert consume(True, "snapshot-a", "snapshot-a", 3, "forged-generation", True)[0] == "stock"
    print("PROFILE_AUTHORIZATION_LIFECYCLE_SNAPSHOT_CONSUME_REPLAY PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
