#!/usr/bin/env bash
set -u

SOURCE=cb1e8199d35c894d0400a0bf4eef2aa626616026
TARGET=f78cb2fd1894c2e1f7197d121a4d73db980c85a7
FINAL=12ac8b86cfd82d1439ae47d4b7b198b26f0e545a
DOC_AUTH=8448b57408bb4dc65fc8c94d2cad5863de939797
PROD_AUTH=feb137e459a329e01ff3ee87d3e3acb04a942d72
EVIDENCE_COMMIT=18ab58e1280191cba25b803c33f3a36e9e10983f

REPO_ROOT=$(git rev-parse --show-toplevel)
OUT="$REPO_ROOT/pmux_project/experiments/mechanical_integration/a_12ac8b8_audit/evidence"
mkdir -p "$OUT"

run_record() {
    local name=$1
    shift
    set +e
    "$@" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"
    local rc=$?
    set -e
    printf '%s\n' "$rc" >"$OUT/$name.rc"
}

set -e
run_record 01_remote_gate git ls-remote https://github.com/Coldflame-Dawnson/pmux-yosys.git \
    refs/heads/feature/b-g3-mechanical-integration-cb1e819 \
    refs/heads/feature/b-v02-documentation-closure \
    refs/heads/integration/g3 \
    refs/heads/feature/b-g3-mechanical-evidence-12ac8b8 \
    refs/heads/feature/a-v02-documentation-closure-audit-cb1e819 \
    refs/heads/feature/a-v02-unified-case12-default-audit-dbb21ec
run_record 02_commit_object git show -s --format='%H%n%P%n%T' "$FINAL"
run_record 03_source_ancestor git merge-base --is-ancestor "$SOURCE" "$FINAL"
run_record 04_target_ancestor_source git merge-base --is-ancestor "$TARGET" "$SOURCE"
run_record 05_rev_list git rev-list --left-right --count "$TARGET...$SOURCE"
run_record 06_whole_tree_diff git diff --exit-code "$SOURCE" "$FINAL"
run_record 07_raw_diff git diff --raw --no-abbrev "$SOURCE" "$FINAL"
run_record 08_submodule_diff git diff --submodule=log "$SOURCE" "$FINAL"
run_record 09_source_final_diff_check git diff --check "$SOURCE" "$FINAL"
run_record 10_tree_ids git rev-parse "$SOURCE^{tree}" "$FINAL^{tree}"
run_record 11_scoped_diff git diff --exit-code "$SOURCE" "$FINAL" -- \
    pmux_project/pass pmux_project/analyzer pmux_project/include pmux_project/scripts \
    techlibs/intel/synth_intel.cc design
run_record 12_submodule_status git submodule status --recursive
set +e
git diff --check "$TARGET" "$SOURCE" >"/tmp/a_12ac8b8_target_source_diff_check.txt" 2>&1
rc=$?
set -e
printf '%s\n' "$rc" >"$OUT/13_target_source_diff_check.rc"
: >"$OUT/13_target_source_diff_check.stderr"
{
    printf 'raw_output_sha256='
    sha256sum "/tmp/a_12ac8b8_target_source_diff_check.txt" | cut -d' ' -f1
    printf 'findings='
    grep -Ec '^[^+].*:[0-9]+:' "/tmp/a_12ac8b8_target_source_diff_check.txt"
    printf 'files='
    grep -E '^[^+].*:[0-9]+:' "/tmp/a_12ac8b8_target_source_diff_check.txt" \
        | sed -E 's/:[0-9]+:.*$//' | sort -u | wc -l
} >"$OUT/13_target_source_diff_check.stdout"
run_record 14_worktree_status git status --short
run_record 15_doc_hash bash -lc "cd '$REPO_ROOT/design' && sha256sum -c DOCUMENT_SHA256SUMS"

printf '%s\n' "$DOC_AUTH" >"$OUT/document_authorization.txt"
printf '%s\n' "$PROD_AUTH" >"$OUT/production_authorization.txt"
printf '%s\n' "$EVIDENCE_COMMIT" >"$OUT/b_evidence_commit.txt"
