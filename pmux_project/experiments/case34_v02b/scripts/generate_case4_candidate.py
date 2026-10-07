#!/usr/bin/env python3
"""Extract the official 32x10 table and emit a sequential cofactor candidate."""

import argparse
import json
import re
from pathlib import Path

DEFAULT_SPLITS = (3, 3, 1, 4, 4, 4, 0, 2, 4, 4)

ROW_RE = re.compile(
    r"5'd(?P<address>\d+)\s*:\s*"
    r"\{\s*dataout\s*,\s*only_5b\s*,\s*datain_5bis7\s*,\s*data_dp\s*,\s*data_dn\s*\}"
    r"\s*<=\s*\{\s*6'b(?P<data>[01]{6})\s*,\s*1'b(?P<only>[01])\s*,"
    r"\s*1'b(?P<is7>[01])\s*,\s*1'b(?P<dp>[01])\s*,\s*1'b(?P<dn>[01])\s*\}\s*;"
)


def parse_splits(text):
    values = tuple(int(item) for item in text.split(','))
    if len(values) != 10 or any(value < 0 or value > 4 for value in values):
        raise argparse.ArgumentTypeError('expected ten comma-separated values in [0,4]')
    return values


def extract_words(source):
    rows = {}
    for match in ROW_RE.finditer(source):
        address = int(match.group('address'))
        bits = ''.join(match.group(name) for name in ('data', 'only', 'is7', 'dp', 'dn'))
        if address in rows:
            raise ValueError(f'duplicate address {address}')
        rows[address] = int(bits, 2)
    missing = sorted(set(range(32)) - set(rows))
    extra = sorted(set(rows) - set(range(32)))
    if missing or extra:
        raise ValueError(f'incomplete table: missing={missing}, extra={extra}')
    return tuple(rows[address] for address in range(32))


def cofactor(words, output_bit, split_bit, select_value):
    remaining = tuple(bit for bit in range(5) if bit != split_bit)
    mask = 0
    for compact in range(16):
        address = select_value << split_bit
        for index, source_bit in enumerate(remaining):
            address |= ((compact >> index) & 1) << source_bit
        mask |= ((words[address] >> output_bit) & 1) << compact
    return remaining, mask


def emit_candidate(words, splits):
    shared = {}
    declarations = []
    assignments = []
    records = []
    for output_bit, split_bit in enumerate(splits):
        branches = []
        for select_value in (0, 1):
            remaining, mask = cofactor(words, output_bit, split_bit, select_value)
            # Constants are independent of support and can be shared globally.
            key = ((), mask) if mask in (0x0000, 0xffff) else (remaining, mask)
            if key not in shared:
                name = f'cf_{len(shared)}'
                shared[key] = name
                index = '{' + ','.join(f'datain[{bit}]' for bit in reversed(remaining)) + '}'
                if mask in (0x0000, 0xffff):
                    declarations.append(f'wire {name} = 1\'b{1 if mask else 0};')
                else:
                    declarations.extend([
                        f'wire {name};',
                        f'case4_lut4 #(.LUT(16\'h{mask:04x})) {name}_cell (.A({index}), .Y({name}));',
                    ])
            branches.append(shared[key])
            records.append({
                'output_bit': output_bit,
                'split_bit': split_bit,
                'select_value': select_value,
                'remaining_bits': list(remaining),
                'truth_mask_hex': f'{mask:04X}',
                'shared_name': shared[key],
            })
        assignments.append(
            f'assign lookup_word[{output_bit}] = datain[{split_bit}] ? {branches[1]} : {branches[0]};'
        )

    body = '\n    '.join(declarations + assignments)
    verilog = f'''module case4_lut4 #(parameter [15:0] LUT = 16'h0000)
(
    input [3:0] A,
    output Y
);
    assign Y = LUT[A];
endmodule

module test4(
    input clk,
    input reset_n,
    input ena,
    input [4:0] datain,
    output reg [5:0] dataout,
    output reg only_5b,
    output reg datain_5bis7,
    output reg data_dp,
    output reg data_dn
);
    wire [9:0] lookup_word;
    {body}

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            dataout <= 6'b000000;
            only_5b <= 1'b1;
            data_dp <= 1'b0;
            data_dn <= 1'b0;
            // Deliberately no assignment to datain_5bis7: preserve hold semantics.
        end else if (!ena) begin
            {{dataout, only_5b, datain_5bis7, data_dp, data_dn}} <= 10'b0;
        end else begin
            {{dataout, only_5b, datain_5bis7, data_dp, data_dn}} <= lookup_word;
        end
    end
endmodule
'''
    return verilog, records


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--official', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--table-json', required=True, type=Path)
    parser.add_argument('--cofactor-json', required=True, type=Path)
    parser.add_argument('--splits', type=parse_splits, default=DEFAULT_SPLITS)
    args = parser.parse_args()

    words = extract_words(args.official.read_text(encoding='utf-8'))
    candidate, records = emit_candidate(words, args.splits)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(candidate, encoding='utf-8')
    args.table_json.write_text(json.dumps({
        'word_order': ['dataout[5:0]', 'only_5b', 'datain_5bis7', 'data_dp', 'data_dn'],
        'rows': [{'address': address, 'bits': f'{word:010b}', 'hex': f'{word:03X}'}
                 for address, word in enumerate(words)],
    }, indent=2) + '\n', encoding='utf-8')
    args.cofactor_json.write_text(json.dumps({
        'splits_by_word_bit_0_to_9': list(args.splits),
        'unique_cofactors': len({item['shared_name'] for item in records}),
        'cofactors': records,
    }, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
