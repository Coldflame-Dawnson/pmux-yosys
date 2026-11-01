#!/usr/bin/env bash
set -euo pipefail

repo=${1:-$(git rev-parse --show-toplevel)}
prior_a_worktree=${2:-}
out="$repo/pmux_project/experiments/documentation_closure/a_cb1e819_audit/evidence"
target=cb1e8199d35c894d0400a0bf4eef2aa626616026
parent=dbb21ecd5d50d5636aec7415d9a8e6adb9b5f300
prior_a=feb137e459a329e01ff3ee87d3e3acb04a942d72

mkdir -p "$out"
cd "$repo"

git ls-remote https://github.com/Coldflame-Dawnson/pmux-yosys.git \
  refs/heads/feature/b-v02-documentation-closure > "$out/remote.txt"
test "$(cut -f1 "$out/remote.txt")" = "$target"
test "$(git rev-parse HEAD)" = "$target"
test "$(git rev-parse HEAD^)" = "$parent"

{
  printf 'HEAD='; git rev-parse HEAD
  printf 'PARENT='; git rev-parse HEAD^
  printf 'TREE='; git rev-parse HEAD^{tree}
  printf 'PARENT_TREE='; git rev-parse HEAD^^{tree}
  printf '\nNAME_STATUS\n'
  git -c core.quotepath=false diff --name-status "$parent" HEAD
  printf '\nRAW\n'
  git diff --raw "$parent" HEAD
  printf '\nSTATUS\n'
  git status --short
} > "$out/git-scope.txt"

git diff --exit-code > "$out/a-working-tree.diff"
git diff --cached --exit-code > "$out/a-index.diff"
test -z "$(git status --short | grep -v '^?? pmux_project/experiments/documentation_closure/' || true)"

test "$(git -c core.quotepath=false diff --name-only "$parent" HEAD | wc -l)" -eq 10
test "$(git -c core.quotepath=false diff --name-only "$parent" HEAD | grep -vc '^design/')" -eq 0
git diff --exit-code "$parent" HEAD -- . ':!design' > "$out/non-design.diff"
git diff --check "$parent" HEAD > "$out/diff-check.txt"
test -z "$(git diff --raw "$parent" HEAD | awk '$5 != "A" || $2 != "100644" {print}')"
test -z "$(git diff --name-only "$parent" HEAD -- .gitmodules)"

(cd design && sha256sum -c DOCUMENT_SHA256SUMS) > "$out/document-sha256-check.txt"
test "$(grep -c ': OK$' "$out/document-sha256-check.txt")" -eq 9

python3 - "$repo" > "$out/document-structure.json" <<'PY'
import json, pathlib, re, sys, urllib.parse
repo = pathlib.Path(sys.argv[1])
root = repo / "design"
files = sorted(p for p in root.rglob("*") if p.is_file())
md = [p for p in files if p.suffix == ".md"]
links = []
for path in md:
    text = path.read_text(encoding="utf-8")
    for target in re.findall(r"(?<!!)\[[^]]*\]\(([^)]+)\)", text):
        token = target.strip().split()[0].strip("<>")
        if token.startswith(("http://", "https://", "#")):
            continue
        resolved = (path.parent / urllib.parse.unquote(token.split("#", 1)[0])).resolve()
        links.append({
            "source": str(path.relative_to(root)),
            "target": token,
            "exists_in_checkout": resolved.exists(),
        })
result = {
    "files": len(files),
    "markdown_files": len(md),
    "checksum_entries": len((root / "DOCUMENT_SHA256SUMS").read_text().splitlines()),
    "crlf_files": [str(p.relative_to(root)) for p in files if b"\r\n" in p.read_bytes()],
    "relative_link_occurrences": len(links),
    "missing_relative_link_occurrences": sum(not x["exists_in_checkout"] for x in links),
    "missing_relative_link_targets": sorted({x["target"] for x in links if not x["exists_in_checkout"]}),
}

def count_external(text, base):
    count = 0
    for target in re.findall(r"(?<!!)\[[^]]*\]\(([^)]+)\)", text):
        token = target.strip().split()[0].strip("<>")
        if token.startswith(("http://", "https://")):
            count += 1
        elif not token.startswith("#"):
            resolved = (base / urllib.parse.unquote(token.split("#", 1)[0])).resolve()
            count += not resolved.exists()
    return count

current_v02 = (root / "PMUX优化技术方案_v0.2.md").read_text(encoding="utf-8")
archive_v02 = (root / "archive/PMUX优化技术方案_v0.2_收口前历史快照.md").read_text(encoding="utf-8")
marker = "## 0. 文档状态、范围与证据约定"
result["disclosed_historical_external_reference_occurrences"] = (
    count_external(current_v02[current_v02.index(marker):], root)
    + count_external(archive_v02[archive_v02.index(marker):], root)
    + count_external((root / "工程状态.md").read_text(encoding="utf-8"), root)
    + count_external((root / "统一默认路径交付状态_2026-11-01.md").read_text(encoding="utf-8"), root)
)
print(json.dumps(result, ensure_ascii=False, indent=2))
PY

if [[ -n "$prior_a_worktree" ]]; then
  test "$(git -C "$prior_a_worktree" rev-parse HEAD)" = "$prior_a"
  test -z "$(git -C "$prior_a_worktree" status --porcelain)"
  prior_dir="$prior_a_worktree/pmux_project/experiments/case12_v02/a_unified_default_audit"
  (cd "$prior_dir" && sha256sum -c SHA256SUMS) > "$out/prior-a-sha256-check.txt"
  test "$(grep -c ': OK$' "$out/prior-a-sha256-check.txt")" -eq 47
  test "$(wc -l < "$prior_dir/SHA256SUMS")" -eq 47
  test "$(find "$prior_dir" -type f | wc -l)" -eq 48
fi

printf 'PASS\n' > "$out/RESULT"
