# 给 A 的 prompt：G3机械集成候选最终短复核

你是PMUX的A，负责证明、独立审计与最终验收。当前A/B双人制，B已按项目负责人明确指令完成独立机械合并候选。本轮请只做最终tree/diff/hash和必要短检查，不替B修生产代码，不直接更新integration/g3。

## 项目负责人已确认的范围

目标integration/g3冻结为`f78cb2fd1894c2e1f7197d121a4d73db980c85a7`；来源严格`cb1e8199d35c894d0400a0bf4eef2aa626616026`；普通`--no-ff` merge commit。先候选供你复核，通过后B才更新目标。交付case4为已验27 `$lut`，26保留研究；不合入额外A审计分支；本轮不执行release/eLinx。完整历史P2为UNVERIFIED，外部计分为UNVERIFIED_EXTERNAL_SCORING。

## 精确对象

- 团队私有仓库：`https://github.com/Coldflame-Dawnson/pmux-yosys.git`
- 待审候选分支：`feature/b-g3-mechanical-integration-cb1e819`
- 待审完整提交：`12ac8b86cfd82d1439ae47d4b7b198b26f0e545a`
- 第一父提交/冻结目标：`f78cb2fd1894c2e1f7197d121a4d73db980c85a7`
- 第二父提交/授权来源：`cb1e8199d35c894d0400a0bf4eef2aa626616026`
- 来源与候选tree：`f23b81e9706304fbbfa9209a542c8e0f40e84ac7`
- 前次文档A授权：`8448b57408bb4dc65fc8c94d2cad5863de939797`
- 前次生产A验收：`feb137e459a329e01ff3ee87d3e3acb04a942d72`，绑定`dbb21ecd5d50d5636aec7415d9a8e6adb9b5f300`
- B证据分支：`feature/b-g3-mechanical-evidence-12ac8b8`。该分支仅承载B本轮核验报告与原始输出，**不是集成来源，不合入待审候选**。读取前记录它的完整远端哈希；B交付另给其精确提交。

先独立ls-remote核验以上候选、来源、目标、证据分支；来源或目标哈希变化立即停止，不默认采用“最新”。在Linux文件系统新建独立审计worktree，例如`feature/a-g3-mechanical-final-audit-12ac8b8`。不要覆盖原A证据或B分支。

## 必读材料

1. B证据分支的`pmux_project/experiments/mechanical_integration/b_12ac8b8/DELIVERY.md`、`RESULTS.json`、`commands.json`、`SHA256SUMS`；原始90份编号stdout/stderr在`raw-command-output.tar.gz`内，解压到独立临时目录后先验内部SHA256SUMS。
2. 候选中`design/统一默认路径交付状态_2026-11-01.md`、v0.2顶部当前状态、双人契约及前次A授权报告。
3. B证据中披露的暂存diff-check告警，见下节；不能把rc=2藏成全部通过。

## 核验命令

在候选提交独立工作树执行（先fetch精确refs）：

```bash
SOURCE=cb1e8199d35c894d0400a0bf4eef2aa626616026
TARGET=f78cb2fd1894c2e1f7197d121a4d73db980c85a7
FINAL=12ac8b86cfd82d1439ae47d4b7b198b26f0e545a
git status --short
git show -s --format='%H%n%P%n%T' "$FINAL"
git merge-base --is-ancestor "$SOURCE" "$FINAL"
git merge-base --is-ancestor "$TARGET" "$SOURCE"
git rev-list --left-right --count "$TARGET...$SOURCE"
git diff --exit-code "$SOURCE" "$FINAL"
git diff --raw --no-abbrev "$SOURCE" "$FINAL"
git diff --submodule=log "$SOURCE" "$FINAL"
git diff --check "$SOURCE" "$FINAL"
git rev-parse "$SOURCE^{tree}" "$FINAL^{tree}"
git diff --exit-code "$SOURCE" "$FINAL" -- pmux_project/pass \
  pmux_project/analyzer pmux_project/include pmux_project/scripts \
  techlibs/intel/synth_intel.cc design
git submodule status --recursive
(cd design && sha256sum -c DOCUMENT_SHA256SUMS)
```

期望：两父提交顺序准确；rev-list为0/56；来源和候选整tree完全相等，diff/raw/submodule均为空；文档9/9通过，工作树干净。整tree比较同时覆盖构建、接口、测试期望、所有文件模式和gitlink；没有任何额外文件需要保留分类。

再于原A证据精确提交的独立worktree执行：

```bash
cd pmux_project/experiments/case12_v02/a_unified_default_audit
sha256sum -c SHA256SUMS
```

期望47/47。B已执行该检查，不能以B输出替代你自己的核验。

## 继承空白告警，必须独立分类

实际`git diff --cached --check`对老目标返回rc=2，99条、11文件。只涉及来源既有case34历史Markdown和原始Yosys日志尾随空白/EOF空行。首次B脚本因此停下，续执行脚本先证明index tree==SOURCE tree，且以下输出字节完全一致，才提交：

```bash
git diff --check "$TARGET" "$SOURCE"
# 与B的17.stdout及后续暂存检查输出比较，预期同样rc=2
```

没有手工解决、清理原文件或修日志；`git diff --check SOURCE FINAL`为空且rc=0。请明确写出你对“继承来源格式告警”是否阻塞机械集成的判定依据。若仍认为必须修改源文件，给BLOCKED和请求，不直接清理、amend或放宽门禁。

新worktree子模块为未初始化状态，gitlinks不变，B没有编译或声称初始化。若你需要短编译，自行按冻结提交初始化并记录；不因未初始化而声称发生子模块变更。

## 不重跑长实验的条件

只有整tree与来源一致、精确授权链与证据闭包有效、项目负责人范围不变时，才引用上一轮19微测试、1000+1000、四例EQY与G3技术PASS；明确这些是前次生产证据而非本次新跑。生产内容、模式、构建、oracle、文档或gitlink任何变化，BLOCKED并退B新提交。

## 最终交付

只新增你的独立审计目录，形成新A提交并推送。不要把A证据提交到机械候选，也不要直接更新integration/g3或执行release/eLinx。

返回：

1. AUTHORIZED/BLOCKED，严格绑定`12ac8b86cfd82d1439ae47d4b7b198b26f0e545a`。
2. A分支/完整HEAD，待审B、全部父提交与共同tree。
3. 整tree/生产/文档/模式/gitlink/额外文件检查，9/9和47/47结果。
4. 继承空白告警的独立判定；是否有冲突或手工修改证据。
5. 是否允许B将integration/g3从冻结`f78cb2fd...`更新到精确候选`12ac8b86...`；仅当执行时远端仍等于冻结目标才允许，无强推。
6. 完整历史P2仍UNVERIFIED；外部计分仍UNVERIFIED_EXTERNAL_SCORING；release/eLinx仍NOT_EXECUTED。
7. 本地/tracking/远端一致性、工作树和证据清单/哈希闭包。

若远端目标已移动，不沿用此前冻结值做覆盖更新，报告变化并重新冻结。本轮授权不能被解释为对其他后继提交、证据分支或生产修改的许可。
