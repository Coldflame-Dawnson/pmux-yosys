#!/usr/bin/env python3
"""Evaluate every Shannon split variable for the case4 constant ROM."""

import argparse
import json
import subprocess
from pathlib import Path


WORDS = [int(x, 2) for x in (
    "1001110000", "0111010000", "1011010000", "1100011000",
    "1101010000", "1010011000", "0110011000", "1110000100",
    "1110010000", "1001011000", "0101011000", "1101001001",
    "0011011000", "1011001001", "0111001001", "0101110000",
    "0110110000", "1000111010", "0100111010", "1100101000",
    "0010111010", "1010101000", "0110101000", "1110100000",
    "1100110000", "1001101000", "0101101000", "1101100000",
    "0011101000", "1011100000", "0111100000", "1010110000",
)]

MAPPERS = {
    "default": None,
    "dch_equiv": "&scorr; &sweep; &dc2; &dch -e -r; &if -W 300 -v; &mfs",
    "dch_equiv_area2": "&scorr; &sweep; &dc2; &dch -e -r; &if -a -W 300 -v; &mfs; &mfs",
}


def q(path: Path) -> str:
    return '"' + path.as_posix().replace('"', '\\"') + '"'


def make_source(split: int) -> str:
    remaining = [bit for bit in range(5) if bit != split]
    addr_expr = "{" + ", ".join(f"datain[{bit}]" for bit in reversed(remaining)) + "}"
    inits = []
    for out_bit in range(10):
        pair = []
        for split_value in (0, 1):
            truth = 0
            for index4 in range(16):
                address = split_value << split
                for pos, source_bit in enumerate(remaining):
                    address |= ((index4 >> pos) & 1) << source_bit
                truth |= ((WORDS[address] >> out_bit) & 1) << index4
            pair.append(truth)
        inits.append(pair)

    cells = []
    for bit, (low, high) in enumerate(inits):
        cells.append(f"lut4 #(.INIT(16'h{low:04x})) lo{bit} (.a(addr4), .y(lo[{bit}]));")
        cells.append(f"lut4 #(.INIT(16'h{high:04x})) hi{bit} (.a(addr4), .y(hi[{bit}]));")
    return f"""module lut4 #(parameter [15:0] INIT = 16'h0000) (
    input wire [3:0] a, output wire y);
assign y = INIT[a];
endmodule

module test4(input wire clk, reset_n, ena, input wire [4:0] datain,
    output reg [5:0] dataout, output reg only_5b,
    output reg datain_5bis7, output reg data_dp, output reg data_dn);
wire [3:0] addr4 = {addr_expr};
wire [9:0] lo, hi;
wire [9:0] rom_value = datain[{split}] ? hi : lo;
{chr(10).join(cells)}
always @(posedge clk or negedge reset_n) begin
    if (!reset_n) begin
        dataout <= 6'b0; only_5b <= 1'b1; data_dp <= 1'b0; data_dn <= 1'b0;
    end else if (!ena) begin
        {{dataout, only_5b, datain_5bis7, data_dp, data_dn}} <= 10'b0;
    end else begin
        {{dataout, only_5b, datain_5bis7, data_dp, data_dn}} <= rom_value;
    end
end
endmodule
"""


def count_luts(path: Path) -> int:
    doc = json.loads(path.read_text(encoding="utf-8"))
    return sum(cell["type"] == "$lut"
               for cell in doc["modules"]["test4"].get("cells", {}).values())


def run_one(yosys: Path, source: Path, work: Path, mapper_name: str,
            mapper: str | None) -> dict:
    netlist = work / f"{mapper_name}.json"
    commands = [
        f"read_verilog {q(source)}",
        "synth_intel -family cycloneiv -top test4 -run begin:map_ffram",
        "opt -fast -mux_undef -undriven -fine -full",
        "memory_map",
        "opt -undriven -fine",
        "opt -full",
        "techmap -map +/techmap.v",
        "opt -full",
        "clean -purge",
        "setundef -undriven -zero",
        "synth_intel -family cycloneiv -top test4 -run map_ffs:map_luts",
    ]
    if mapper is None:
        commands.append("synth_intel -family cycloneiv -top test4 -run map_luts:map_cells")
    else:
        abc_file = work / f"{mapper_name}.abc"
        abc_file.write_text(mapper + "\n", encoding="utf-8")
        commands.extend([
            f"abc9 -lut 4 -W 300 -script {q(abc_file)}",
            "clean", "opt -fast", "autoname", "clean",
        ])
    commands.extend(["check", f"write_json {q(netlist)}"])
    (work / f"{mapper_name}.ys").write_text(";\n".join(commands) + ";\n",
                                             encoding="utf-8")
    process = subprocess.run([str(yosys), "-Q", "-p", "; ".join(commands)],
                             text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT)
    (work / f"{mapper_name}.log").write_text(process.stdout, encoding="utf-8")
    if process.returncode or not netlist.is_file():
        return {"mapper": mapper_name, "status": "FAIL",
                "returncode": process.returncode}
    return {"mapper": mapper_name, "status": "PASS", "lut": count_luts(netlist)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--yosys", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.yosys = args.yosys.resolve()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)

    rows = []
    for split in range(5):
        work = args.out / f"split_{split}"
        work.mkdir(parents=True, exist_ok=True)
        source = work / "test4.v"
        source.write_text(make_source(split), encoding="utf-8")
        for mapper_name, mapper in MAPPERS.items():
            row = run_one(args.yosys, source, work, mapper_name, mapper)
            row["split"] = split
            rows.append(row)
    passed = [row for row in rows if row["status"] == "PASS"]
    best = min(row["lut"] for row in passed)
    print(f"best={best} LUT")
    for row in passed:
        if row["lut"] == best:
            print(f"split={row['split']} mapper={row['mapper']}")
    (args.out / "summary.json").write_text(json.dumps({
        "schema": 1, "baseline_lut": 36, "results": rows,
    }, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
