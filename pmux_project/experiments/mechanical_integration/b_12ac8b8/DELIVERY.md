# B 机械集成候选交付

日期：2026-11-01。项目负责人已确认目标、普通merge方式及27 `$lut`交付范围。本轮只建立候选，等待A最终短复核，不更新integration/g3、不release、不eLinx。

| 对象 | 值 |
|---|---|
| 候选分支 | `feature/b-g3-mechanical-integration-cb1e819` |
| 候选完整提交 | `12ac8b86cfd82d1439ae47d4b7b198b26f0e545a` |
| 第一父提交/冻结目标 | `f78cb2fd1894c2e1f7197d121a4d73db980c85a7` |
| 第二父提交/来源 | `cb1e8199d35c894d0400a0bf4eef2aa626616026` |
| 候选与来源共同tree | `f23b81e9706304fbbfa9209a542c8e0f40e84ac7` |
| 文档A授权 | `8448b57408bb4dc65fc8c94d2cad5863de939797` |
| 生产A验收 | `feb137e459a329e01ff3ee87d3e3acb04a942d72`，绑定dbb21ec |
| 独立证据分支 | `feature/b-g3-mechanical-evidence-12ac8b8`；不进入候选树 |

实际合并命令（在独立Linux工作树、从冻结目标建新分支后）：

```bash
git merge --no-commit --no-ff cb1e8199d35c894d0400a0bf4eef2aa626616026
git commit -m 'Merge audited PMUX v0.2 default path into G3 candidate' <已记录的状态说明>
```

完整argv、cwd、退出码见证据`commands.json`；90份编号stdout/stderr原始字节无损存于`raw-command-output.tar.gz`，内部逐文件校验已通过。没有手工解决冲突或额外文件修改，没有把A报告合入候选。

## 实际核验

- 目标独有提交0；来源较目标新增56提交，目标为来源祖先。
- merge无冲突；提交前index tree等于来源tree。
- 最终两父提交顺序为冻结目标、来源；来源为最终提交祖先。
- **最终整个tree与来源相同**，不只检查少数生产路径；文件内容、模式、子模块gitlink、构建、接口、测试oracle和design均无差异。
- 来源到最终的name-status/raw/submodule/diff-check全部为空。
- design文档SHA256：9/9 PASS；原A47条SHA256SUMS：47/47 PASS。
- 候选工作树干净。新工作树的子模块为未初始化状态（`git submodule status`有`-`），gitlink不变；本轮不编译、不宣称初始化或重跑长实验。

## 必须披露的检查告警

`git diff --cached --check`相对老目标返回rc=2：99条告警、11个文件，均为已授权来源内历史Markdown/原始Yosys日志的尾随空白或EOF空行。首次驱动因此停止；后续只读比较`git diff --check TARGET SOURCE`与暂存检查输出，**字节完全一致**，并先核验index tree等于SOURCE tree，才继续提交。

这不是“所有diff-check都通过”。没有清理原日志、改源文件、绕过冲突或更改tree。最终`git diff --check SOURCE FINAL`为空且rc=0。请A独立判断此继承告警分类，不将其隐瞒为零警告。

## 状态边界

| 项目 | 状态 |
|---|---|
| 候选最终A短复核 | PENDING |
| 已验生产技术门禁/G3技术门禁 | 引用前次A PASS，不能由B对新hash自授 |
| 完整历史P2 | UNVERIFIED |
| 外部计分 | UNVERIFIED_EXTERNAL_SCORING |
| integration/g3更新 | 本轮未执行，仍冻结为f78cb2fd... |
| release/eLinx | NOT_EXECUTED |

case4按已验27 `$lut`交付，历史26 `$lut`留研究；26厂商原语+1 `$not`不替代26 `$lut`。只有A对精确候选复核通过且目标远端仍为冻结哈希后，B才可按已确认指令更新目标；若目标变化，立即停下重新冻结。

证据随独立分支发布于`pmux_project/experiments/mechanical_integration/b_12ac8b8/`；候选分支不包含证据新增文件，因此仍与授权来源全树相同。
