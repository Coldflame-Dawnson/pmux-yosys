#!/usr/bin/env python3
"""Run fixed-tool case3/case4 baseline, candidate synthesis, and EQY."""

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest().upper()


def run(command, log_path, env=None):
    log_path.parent.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    with log_path.open('w', encoding='utf-8') as log:
        completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                   text=True, env=env, check=False)
    return completed.returncode, time.perf_counter() - started


def count_types(path, top):
    document = json.loads(path.read_text(encoding='utf-8'))
    cells = document['modules'][top].get('cells', {})
    counts = {}
    for cell in cells.values():
        counts[cell['type']] = counts.get(cell['type'], 0) + 1
    return dict(sorted(counts.items()))


def count_sequential_bits(path, top):
    document = json.loads(path.read_text(encoding='utf-8'))
    cells = document['modules'][top].get('cells', {})
    sequential = {'dffeas', '$adff', '$dff', '$dffe', '$sdff', '$sdffe', '$sdffce'}
    return sum(len(cell.get('connections', {}).get('Q',
                                                    cell.get('connections', {}).get('q', [])))
               for cell in cells.values() if cell['type'] in sequential)


def write_synth_script(source, top, output, custom=False, abc_script=None, lut_map=None):
    output.mkdir(parents=True, exist_ok=True)
    if custom:
        commands = [
            f'read_verilog {source}',
            'proc',
            'techmap -map +/techmap.v',
            f'techmap -map {lut_map}',
            f'hierarchy -check -top {top}',
            'opt',
            f'synth_intel -family cycloneiv -top {top} -run map_ffs:map_luts',
            f'abc9 -lut 4 -W 300 -script {abc_script}',
            'clean',
            'opt -fast',
        ]
    else:
        commands = [
            f'read_verilog {source}',
            f'synth_intel -family cycloneiv -top {top} -run begin:map_cells',
        ]
    commands.extend([
        'check',
        'stat',
        f'select {top}',
        f'write_rtlil -selected {output / "post_abc9.il"}',
        'select -clear',
        f'write_json {output / "post_abc9.json"}',
    ])
    script = output / 'run.ys'
    script.write_text('\n'.join(commands) + '\n', encoding='utf-8')
    return script


def write_eqy(path, gold, gate, top):
    path.write_text(f'''[gold]
read_verilog -formal {gold}

[gate]
read_verilog -formal {gate}

[script]
hierarchy -check -top {top}
prep
memory_map
async2sync

[partition {top}]
merge *

[strategy sat]
use sat
depth 20

[strategy pdr]
use sby
engine abc pdr
timeout 300
''', encoding='utf-8')


def write_gate_eqy(path, gold, gate_il, top, formal_dffeas=None):
    gate_lines = [f'read_rtlil {gate_il}']
    if formal_dffeas is not None:
        gate_lines.append(f'read_verilog -formal -overwrite {formal_dffeas}')
    path.write_text(f'''[gold]
read_verilog -formal {gold}

[gate]
{chr(10).join(gate_lines)}

[script]
hierarchy -check -top {top}
prep
memory_map
async2sync

[partition {top}]
merge *

[strategy sat]
use sat
depth 20

[strategy pdr]
use sby
engine abc pdr
timeout 300
''', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', required=True, type=Path)
    parser.add_argument('--workspace', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()

    tool_bin = args.repo / 'pmux_project/official_baseline/toolchain/install/bin'
    yosys = tool_bin / 'yosys'
    eqy = tool_bin / 'eqy'
    official = args.repo / 'pmux_project/pmux_case/competition_case'
    case3_gold = official / 'test3.v'
    case4_gold = official / 'test4.v'
    case3_gate = args.workspace / 'case3_guard_candidate.v'
    generated = args.out / 'generated'
    generated.mkdir(parents=True, exist_ok=True)
    case4_gate = generated / 'case4_cofactor_candidate.v'

    generator = args.workspace / 'generate_case4_candidate.py'
    subprocess.run([
        sys.executable, str(generator), '--official', str(case4_gold),
        '--output', str(case4_gate),
        '--table-json', str(generated / 'case4_truth_table.json'),
        '--cofactor-json', str(generated / 'case4_cofactors.json'),
    ], check=True)
    abc_script = generated / 'abc.script'
    abc_script.write_text('&scorr; &sweep; &dc2; &dch -e -r; &if -a -W 300 -v; &mfs; &mfs\n',
                          encoding='utf-8')
    lut_map = args.workspace / 'case4_lut4_map.v'

    jobs = [
        ('case3_baseline', case3_gold, 'test3', False),
        ('case3_candidate', case3_gate, 'test3', False),
        ('case4_baseline', case4_gold, 'test4', False),
        ('case4_candidate_default', case4_gate, 'test4', False),
        ('case4_candidate_custom', case4_gate, 'test4', True),
    ]
    results = {}
    for name, source, top, custom in jobs:
        directory = args.out / name
        script = write_synth_script(source, top, directory, custom, abc_script, lut_map)
        returncode, elapsed = run([str(yosys), '-s', str(script)], directory / 'yosys.log')
        result = {'returncode': returncode, 'wall_seconds': elapsed, 'source': str(source)}
        json_path = directory / 'post_abc9.json'
        if returncode == 0 and json_path.exists():
            result['cell_counts'] = count_types(json_path, top)
            result['total_cells'] = sum(result['cell_counts'].values())
            result['sequential_bits'] = count_sequential_bits(json_path, top)
        results[name] = result

    env = os.environ.copy()
    env['PATH'] = str(tool_bin) + os.pathsep + env.get('PATH', '')
    for case, gold, gate, top in (
        ('case3', case3_gold, case3_gate, 'test3'),
        ('case4', case4_gold, case4_gate, 'test4'),
    ):
        directory = args.out / f'{case}_source_eqy'
        directory.mkdir(parents=True, exist_ok=True)
        config = directory / f'{case}.eqy'
        write_eqy(config, gold, gate, top)
        workdir = directory / 'work'
        returncode, elapsed = run(
            [str(eqy), '-f', '-d', str(workdir), str(config)],
            directory / 'eqy.log', env)
        results[f'{case}_source_eqy'] = {
            'returncode': returncode,
            'wall_seconds': elapsed,
            'pass_file': (workdir / 'PASS').exists(),
            'gold_sha256': sha256(gold),
            'gate_sha256': sha256(gate),
        }

    formal_dffeas = args.repo / 'pmux_project/official_baseline/environment/dffeas_formal.v'
    for case, gold, gate_il, top, model in (
        ('case3', case3_gold, args.out / 'case3_candidate/post_abc9.il', 'test3', formal_dffeas),
        ('case4', case4_gold, args.out / 'case4_candidate_custom/post_abc9.il', 'test4', formal_dffeas),
    ):
        directory = args.out / f'{case}_gate_eqy'
        directory.mkdir(parents=True, exist_ok=True)
        config = directory / f'{case}_gate.eqy'
        write_gate_eqy(config, gold, gate_il, top, model)
        workdir = directory / 'work'
        returncode, elapsed = run(
            [str(eqy), '-f', '-d', str(workdir), str(config)],
            directory / 'eqy.log', env)
        results[f'{case}_gate_eqy'] = {
            'returncode': returncode,
            'wall_seconds': elapsed,
            'pass_file': (workdir / 'PASS').exists(),
            'gold_sha256': sha256(gold),
            'gate_sha256': sha256(gate_il),
        }

    for case, top, gate_il in (
        ('case3', 'test3', args.out / 'case3_candidate/post_abc9.il'),
        ('case4', 'test4', args.out / 'case4_candidate_custom/post_abc9.il'),
    ):
        directory = args.out / f'{case}_vendor'
        directory.mkdir(parents=True, exist_ok=True)
        script = directory / 'run.ys'
        script.write_text('\n'.join([
            'read_verilog -sv -lib +/intel/max10/cells_sim.v',
            'read_verilog -sv -lib +/intel/common/m9k_bb.v',
            'read_verilog -sv -lib +/intel/common/altpll_bb.v',
            f'read_rtlil {gate_il}',
            f'hierarchy -check -top {top}',
            'techmap -map +/intel/max10/cells_map.v',
            'clean -purge',
            'check',
            'stat',
            f'write_rtlil {directory / "vendor.il"}',
            f'write_json {directory / "vendor.json"}',
        ]) + '\n', encoding='utf-8')
        returncode, elapsed = run([str(yosys), '-s', str(script)], directory / 'yosys.log')
        result = {'returncode': returncode, 'wall_seconds': elapsed}
        json_path = directory / 'vendor.json'
        if returncode == 0 and json_path.exists():
            result['cell_counts'] = count_types(json_path, top)
            result['total_cells'] = sum(result['cell_counts'].values())
            result['sequential_bits'] = count_sequential_bits(json_path, top)
        results[f'{case}_vendor'] = result

    (args.out / 'summary.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(results, indent=2))
    failed = [name for name, result in results.items() if result['returncode'] != 0]
    if failed:
        raise SystemExit('failed jobs: ' + ', '.join(failed))


if __name__ == '__main__':
    main()
