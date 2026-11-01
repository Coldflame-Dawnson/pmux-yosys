"""One-shot, guarded mechanical merge; evidence stays outside the candidate tree."""
import hashlib
import json
from pathlib import Path
import subprocess

SOURCE = 'cb1e8199d35c894d0400a0bf4eef2aa626616026'
TARGET = 'f78cb2fd1894c2e1f7197d121a4d73db980c85a7'
DOC_AUDIT = '8448b57408bb4dc65fc8c94d2cad5863de939797'
PRODUCTION_AUDIT = 'feb137e459a329e01ff3ee87d3e3acb04a942d72'
BRANCH = 'feature/b-g3-mechanical-integration-cb1e819'
repo = Path('/home/james/work/pmux-yosys-b-case34-production')
work = Path('/home/james/work/pmux-yosys-b-g3-mechanical-cb1e819')
audit_work = Path('/home/james/work/pmux-yosys-b-g3-original-a-evidence')
out = Path('/mnt/d/Work/Embedded_competition/Project_1/B/mechanical_integration_cb1e819')
out.mkdir(parents=True, exist_ok=False)
commands = []

def run(*args, cwd=repo, allowed=(0,)):
    p = subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    number = len(commands) + 1
    record = {'number': number, 'cwd': str(cwd), 'argv': list(args), 'returncode': p.returncode}
    commands.append(record)
    (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    (out / f'{number:02d}.stdout').write_bytes(p.stdout)
    (out / f'{number:02d}.stderr').write_bytes(p.stderr)
    if p.returncode not in allowed:
        raise RuntimeError(f'Command {number} failed: {args!r}; see evidence. STOP, no manual fix.')
    return p.stdout.decode('utf-8').strip()

assert not run('git', 'status', '--porcelain'), 'base worktree dirty'
assert run('git', 'rev-parse', 'origin/feature/b-v02-documentation-closure') == SOURCE
assert run('git', 'rev-parse', 'origin/integration/g3') == TARGET
assert run('git', 'rev-parse', 'origin/feature/a-v02-documentation-closure-audit-cb1e819') == DOC_AUDIT
run('git', 'merge-base', '--is-ancestor', TARGET, SOURCE)
assert run('git', 'rev-list', '--left-right', '--count', f'{TARGET}...{SOURCE}').split() == ['0', '56']
run('git', '-c', 'core.quotePath=false', 'diff', '--name-status', SOURCE, TARGET)
run('git', 'diff', '--raw', '--no-abbrev', SOURCE, TARGET)
run('git', 'diff', '--submodule=log', SOURCE, TARGET)
run('git', 'diff', '--check', SOURCE, TARGET)
assert not work.exists(), 'candidate worktree already exists; refuse overwrite'
run('git', 'worktree', 'add', '-b', BRANCH, str(work), TARGET)
assert not run('git', 'status', '--porcelain', cwd=work)
run('git', 'merge', '--no-commit', '--no-ff', SOURCE, cwd=work)
assert not run('git', 'ls-files', '--unmerged', cwd=work), 'merge conflicts: STOP'
run('git', 'status', '--short', cwd=work)
run('git', '-c', 'core.quotePath=false', 'diff', '--cached', '--name-status', cwd=work)
run('git', 'diff', '--cached', '--check', cwd=work)
run('git', 'diff', '--cached', '--submodule=log', cwd=work)
source_tree = run('git', 'rev-parse', f'{SOURCE}^{{tree}}')
assert run('git', 'write-tree', cwd=work) == source_tree, 'index is not EXACT source tree; STOP'
run('git', 'diff', '--exit-code', cwd=work)
run('git', 'commit', '-m', 'Merge audited PMUX v0.2 default path into G3 candidate',
    '-m', f'Frozen target: {TARGET}\nAuthorized source: {SOURCE}\n'
          f'A documentation authorization: {DOC_AUDIT}\n'
          'Mechanical merge only; source tree unchanged. Pending A final review.\n'
          'Historical P2 UNVERIFIED; external scoring UNVERIFIED_EXTERNAL_SCORING.\n'
          'Release and eLinx NOT_EXECUTED. integration/g3 not updated.', cwd=work)
final = run('git', 'rev-parse', 'HEAD', cwd=work)
parents = run('git', 'show', '-s', '--format=%P', final, cwd=work).split()
assert parents == [TARGET, SOURCE], parents
run('git', 'merge-base', '--is-ancestor', SOURCE, final)
assert run('git', 'rev-parse', f'{final}^{{tree}}') == source_tree
run('git', 'diff', '--exit-code', SOURCE, final)
run('git', 'diff', '--exit-code', SOURCE, final, '--', 'pmux_project/pass',
    'pmux_project/analyzer', 'pmux_project/include', 'pmux_project/scripts',
    'techlibs/intel/synth_intel.cc')
run('git', 'diff', '--exit-code', SOURCE, final, '--', 'design')
run('git', '-c', 'core.quotePath=false', 'diff', '--name-status', SOURCE, final)
run('git', 'diff', '--raw', '--no-abbrev', SOURCE, final)
run('git', 'diff', '--submodule=log', SOURCE, final)
run('git', 'submodule', 'status', '--recursive', cwd=work)
run('sha256sum', '-c', 'DOCUMENT_SHA256SUMS', cwd=work / 'design')
assert len((work / 'design/DOCUMENT_SHA256SUMS').read_text().splitlines()) == 9
assert not audit_work.exists(), 'evidence worktree exists; refuse overwrite'
run('git', 'worktree', 'add', '--detach', str(audit_work), PRODUCTION_AUDIT)
audit_dir = audit_work / 'pmux_project/experiments/case12_v02/a_unified_default_audit'
run('sha256sum', '-c', 'SHA256SUMS', cwd=audit_dir)
assert len((audit_dir / 'SHA256SUMS').read_text().splitlines()) == 47
assert not run('git', 'status', '--porcelain', cwd=work)
assert run('git', 'rev-parse', 'origin/integration/g3') == TARGET
result = {'source': SOURCE, 'frozen_target': TARGET, 'branch': BRANCH, 'final': final,
          'parents': parents, 'tree': source_tree, 'tree_identical_to_source': True,
          'conflicts': 0, 'manual_resolution': False, 'production_diff': 0,
          'extra_files_relative_to_source': [], 'document_hashes': '9/9 PASS',
          'original_A_hashes': '47/47 PASS', 'candidate_worktree_clean': True,
          'target_unique_commits': 0, 'source_additional_commits': 56,
          'integration_target_updated': False, 'long_tests_rerun': False,
          'candidate_final_A_review': 'PENDING', 'historical_P2': 'UNVERIFIED',
          'external_scoring': 'UNVERIFIED_EXTERNAL_SCORING',
          'release': 'NOT_EXECUTED', 'eLinx': 'NOT_EXECUTED',
          'submodule_note': 'New worktree may show uninitialized submodules; gitlinks unchanged. No compilation claimed.'}
(out / 'RESULTS.json').write_text(json.dumps(result, indent=2) + '\n')
files = sorted(p for p in out.iterdir() if p.is_file())
(out / 'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in files))
print(json.dumps(result, indent=2))
