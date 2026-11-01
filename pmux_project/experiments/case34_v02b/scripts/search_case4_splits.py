#!/usr/bin/env python3
"""Bounded, deterministic split search over the corrected official table."""

import argparse
import json
import random
import shutil
import subprocess
from pathlib import Path

from generate_case4_candidate import DEFAULT_SPLITS, emit_candidate, extract_words

SEED = 20261006


def count_luts(path):
    document = json.loads(path.read_text(encoding='utf-8'))
    return sum(cell['type'] == '$lut'
               for cell in document['modules']['test4'].get('cells', {}).values())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--official', required=True, type=Path)
    parser.add_argument('--yosys', required=True, type=Path)
    parser.add_argument('--lut-map', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--random-count', type=int, default=500)
    parser.add_argument('--rounds', type=int, default=4)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    work = args.out / 'work'
    work.mkdir(exist_ok=True)
    words = extract_words(args.official.read_text(encoding='utf-8'))
    abc_script = work / 'abc.script'
    abc_script.write_text(
        '&scorr; &sweep; &dc2; &dch -e -r; &if -a -W 300 -v; &mfs; &mfs\n',
        encoding='utf-8')
    trace_path = args.out / 'trace.jsonl'
    trace_path.write_text('', encoding='utf-8')
    seen = set()
    records = []
    best_vector = None
    best_lut = None

    def evaluate(vector, origin):
        nonlocal best_vector, best_lut
        vector = tuple(vector)
        if vector in seen:
            return
        seen.add(vector)
        candidate, _ = emit_candidate(words, vector)
        source = work / 'candidate.v'
        source.write_text(candidate, encoding='utf-8')
        script = work / 'run.ys'
        script.write_text('\n'.join([
            f'read_verilog {source}',
            'proc',
            'techmap -map +/techmap.v',
            f'techmap -map {args.lut_map}',
            'hierarchy -check -top test4',
            'opt',
            'synth_intel -family cycloneiv -top test4 -run map_ffs:map_luts',
            f'abc9 -lut 4 -W 300 -script {abc_script}',
            'clean',
            'opt -fast',
            'check',
            f'write_rtlil {work / "post_abc9.il"}',
            f'write_json {work / "post_abc9.json"}',
        ]) + '\n', encoding='utf-8')
        log = work / 'yosys.log'
        with log.open('w', encoding='utf-8') as stream:
            completed = subprocess.run([str(args.yosys), '-s', str(script)],
                                       stdout=stream, stderr=subprocess.STDOUT,
                                       text=True, check=False)
        lut = count_luts(work / 'post_abc9.json') if completed.returncode == 0 else None
        accepted = lut is not None and (best_lut is None or lut < best_lut)
        if accepted:
            best_lut = lut
            best_vector = vector
            best_dir = args.out / 'best'
            best_dir.mkdir(exist_ok=True)
            shutil.copy2(source, best_dir / 'case4_candidate.v')
            shutil.copy2(script, best_dir / 'run.ys')
            shutil.copy2(log, best_dir / 'yosys.log')
            shutil.copy2(work / 'post_abc9.il', best_dir / 'post_abc9.il')
            shutil.copy2(work / 'post_abc9.json', best_dir / 'post_abc9.json')
        row = {
            'index': len(records),
            'origin': origin,
            'vector': list(vector),
            'returncode': completed.returncode,
            'lut': lut,
            'accepted': accepted,
        }
        records.append(row)
        with trace_path.open('a', encoding='utf-8') as stream:
            stream.write(json.dumps(row, sort_keys=True) + '\n')

    evaluate(DEFAULT_SPLITS, 'historical_vector_on_correct_table')
    for split in range(5):
        evaluate((split,) * 10, 'uniform')

    rng = random.Random(SEED)
    for _ in range(args.random_count):
        vector = [rng.randrange(5) for _ in range(10)]
        # Retain the useful historical bias without fixing a case identity.
        vector[0] = rng.choice((3, 4))
        vector[1] = rng.choice((3, 4))
        vector[2] = rng.randrange(5)
        vector[5] = 4
        evaluate(vector, 'fixed_seed_biased_random')

    greedy = []
    for round_number in range(1, args.rounds + 1):
        start = best_vector
        start_lut = best_lut
        for output_bit in range(10):
            for split in range(5):
                if split == start[output_bit]:
                    continue
                neighbor = list(start)
                neighbor[output_bit] = split
                evaluate(neighbor, f'greedy_round_{round_number}')
        greedy.append({
            'round': round_number,
            'start': list(start),
            'start_lut': start_lut,
            'end': list(best_vector),
            'end_lut': best_lut,
        })
        if best_lut == start_lut:
            break

    summary = {
        'seed': SEED,
        'random_count': args.random_count,
        'evaluated': len(records),
        'best_vector': list(best_vector),
        'best_lut': best_lut,
        'greedy': greedy,
    }
    (args.out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n',
                                           encoding='utf-8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
