#!/usr/bin/env python3
"""Guard the one-shot mapping-profile authorization in both integration paths."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
SCRATCHPAD = 'pmux_opt.accepted_constant_table'


def require_order(path: Path, start_marker: str, get_marker: str,
                  clear_marker: str, first_effect_marker: str) -> None:
    text = path.read_text(encoding="utf-8")
    start = text.index(start_marker)
    get = text.index(get_marker, start)
    clear = text.index(clear_marker, get)
    first_effect = text.index(first_effect_marker, get)
    if not start < get < clear < first_effect:
        raise AssertionError(f"authorization is not consumed before effects in {path}")


def consume(authorized: bool, generated_cell_present: bool) -> tuple[str, bool]:
    profile = "accepted_constant_table" if authorized and generated_cell_present else "stock"
    return profile, False


def main() -> int:
    clear = f'scratchpad_set_bool("{SCRATCHPAD}", false)'
    get = f'scratchpad_get_bool("{SCRATCHPAD}", false)'
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

    profile, authorization = consume(True, True)
    assert profile == "accepted_constant_table" and not authorization
    replay_profile, replay_authorization = consume(authorization, True)
    assert replay_profile == "stock" and not replay_authorization
    assert consume(False, True)[0] == "stock"
    assert consume(True, False)[0] == "stock"
    print("PROFILE_AUTHORIZATION_CONSUME_REPLAY PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
