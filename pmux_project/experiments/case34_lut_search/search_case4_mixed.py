#!/usr/bin/env python3
"""Search mixed per-output Shannon variables for the case4 ROM."""

import argparse
import json
import random
import subprocess
from pathlib import Path

from search_case4_splits import WORDS, q


ABC_SCRIPT = "&scorr; &sweep; &dc2; &dch -e -r; &if -a -W 300 -v; &mfs; &mfs"


def truth_pair(out_bit: int, split: int) -> tuple[int, int, list[int]]:
    remaining = [bit for bit in range(5) if bit != split]
    pair = []
    for split_value in (0, 1):
        truth = 0
        for index4 in range(16):
            address = split_value << split
            for pos, source_bit in enumerate(remaining):
                address |= ((index4 >> pos) & 1) << source_bit
            truth |= ((WORDS[address] >> out_bit) & 1) << index4
        pair.append(truth)
    return pair[0], pair[1], remaining


def make_source(splits: tuple[int, ...]) -> str:
    cells = []
    for bit, split in enumerate(splits):
        low, high, remaining = truth_pair(bit, split)
        addr = "{" + ", ".join(f"datain[{x}]" for x in reversed(remaining)) + "}"
        cells.extend([
            f"lut4 #(.INIT(16'h{low:04x})) lo{bit} (.a({addr}), .y(lo[{bit}]));",
            f"lut4 #(.INIT(16'h{high:04x})) hi{bit} (.a({addr}), .y(hi[{bit}]));",
            f"assign rom_value[{bit}] = datain[{split}] ? hi[{bit}] : lo[{bit}];",
        ])
    return f"""module lut4 #(parameter [15:0] INIT = 16'h0)
    (input wire [3:0] a, output wire y);
assign y = INIT[a];
endmodule
module test4(input wire clk, reset_n, ena, input wire [4:0] datain,
    output reg [5:0] dataout, output reg only_5b,
    output reg datain_5bis7, output reg data_dp, output reg data_dn);
wire [9:0] lo, hi, rom_value;
{chr(10).join(cells)}
always @(posedge clk or negedge reset_n) begin
    if (!reset_n) begin
        dataout <= 6'b0; only_5b <= 1'b1; data_dp <= 1'b0; data_dn <= 1'b0;
    end else if (!ena)
        {{dataout, only_5b, datain_5bis7, data_dp, data_dn}} <= 10'b0;
    else
        {{dataout, only_5b, datain_5bis7, data_dp, data_dn}} <= rom_value;
end
endmodule
"""


def evaluate(yosys: Path, out: Path, splits: tuple[int, ...]) -> dict:
    name = "".join(str(x) for x in splits)
    work = out / name
    work.mkdir(parents=True, exist_ok=True)
    source = work / "test4.v"
    abc_file = work / "abc.script"
    netlist = work / "post_abc9.json"
    source.write_text(make_source(splits), encoding="utf-8")
    abc_file.write_text(ABC_SCRIPT + "\n", encoding="utf-8")
    commands = [
        f"read_verilog {q(source)}",
        "synth_intel -family cycloneiv -top test4 -run begin:map_ffram",
        "opt -fast -mux_undef -undriven -fine -full", "memory_map",
        "opt -undriven -fine", "opt -full", "techmap -map +/techmap.v",
        "opt -full", "clean -purge", "setundef -undriven -zero",
        "synth_intel -family cycloneiv -top test4 -run map_ffs:map_luts",
        f"abc9 -lut 4 -W 300 -script {q(abc_file)}",
        "clean", "opt -fast", "autoname", "clean", "check",
        f"write_json {q(netlist)}",
    ]
    process = subprocess.run([str(yosys), "-Q", "-p", "; ".join(commands)],
                             text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=30)
    (work / "console.log").write_text(process.stdout, encoding="utf-8")
    if process.returncode or not netlist.is_file():
        return {"splits": list(splits), "status": "FAIL"}
    doc = json.loads(netlist.read_text(encoding="utf-8"))
    lut = sum(c["type"] == "$lut" for c in
              doc["modules"]["test4"].get("cells", {}).values())
    return {"splits": list(splits), "status": "PASS", "lut": lut,
            "netlist": netlist.as_posix()}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--yosys", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.yosys = args.yosys.resolve()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)

    candidates = {tuple([split] * 10) for split in range(5)}
    for shared in range(5):
        for first in (3, 4):
            for second in (3, 4):
                candidates.add((first, second, shared, shared, shared, 4,
                                shared, shared, shared, shared))
    rng = random.Random(20261006)
    for _ in range(500):
        vector = [4] * 10
        vector[0] = rng.choice((3, 4))
        vector[1] = rng.choice((3, 4))
        vector[2] = rng.randrange(5)
        vector[5] = 4
        for bit in (3, 4, 6, 7, 8, 9):
            if rng.random() < 0.35:
                vector[bit] = rng.randrange(5)
        candidates.add(tuple(vector))
    rows = [evaluate(args.yosys, args.out, vector)
            for vector in sorted(candidates)]

    best_row = min((row for row in rows if row["status"] == "PASS"),
                   key=lambda row: row["lut"])
    current = tuple(best_row["splits"])
    for _ in range(4):
        neighbors = set()
        for bit in range(10):
            for split in range(5):
                if split != current[bit]:
                    candidate = list(current)
                    candidate[bit] = split
                    neighbors.add(tuple(candidate))
        new_rows = [evaluate(args.yosys, args.out, vector)
                    for vector in sorted(neighbors)]
        rows.extend(new_rows)
        candidate_best = min((row for row in new_rows if row["status"] == "PASS"),
                             key=lambda row: row["lut"])
        if candidate_best["lut"] >= best_row["lut"]:
            break
        best_row = candidate_best
        current = tuple(best_row["splits"])

    print(f"best={best_row['lut']} LUT splits={best_row['splits']}")
    (args.out / "summary.json").write_text(json.dumps({
        "schema": 1, "baseline_lut": 36, "best": best_row, "results": rows,
    }, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
