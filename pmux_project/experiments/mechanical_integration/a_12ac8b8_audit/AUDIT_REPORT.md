# A：G3 机械集成候选最终短复核

## 结论

`AUTHORIZED`，严格绑定候选提交：

```text
12ac8b86cfd82d1439ae47d4b7b198b26f0e545a
```

允许 B 在执行时远端 `integration/g3` 仍精确等于
`f78cb2fd1894c2e1f7197d121a4d73db980c85a7` 的前提下，以普通、非强制的
fast-forward push 将它更新到上述候选。若远端已经移动，必须停止并重新冻结；本授权不覆盖
任何后继提交、B 证据分支、生产修改、release 或 eLinx 操作。

## 精确对象

- 第一父提交/冻结目标：`f78cb2fd1894c2e1f7197d121a4d73db980c85a7`
- 第二父提交/已授权来源：`cb1e8199d35c894d0400a0bf4eef2aa626616026`
- 候选：`12ac8b86cfd82d1439ae47d4b7b198b26f0e545a`
- 来源与候选 tree：`f23b81e9706304fbbfa9209a542c8e0f40e84ac7`
- B 证据提交：`18ab58e1280191cba25b803c33f3a36e9e10983f`
- 前次文档授权：`8448b57408bb4dc65fc8c94d2cad5863de939797`
- 前次生产验收：`feb137e459a329e01ff3ee87d3e3acb04a942d72`

远端门禁在复核开始时全部精确匹配；目标分支仍为冻结提交。

## 独立核验结果

1. 候选是普通双父 merge commit，父顺序精确为 TARGET、SOURCE。
2. `TARGET...SOURCE` 为 `0 56`；TARGET 是 SOURCE 的祖先，SOURCE 是 FINAL 的祖先。
3. SOURCE 与 FINAL 的完整 tree 相同，`diff`、raw diff、submodule diff、生产路径 diff、
   文档 diff 及 `diff --check` 均为空。
4. 子模块 gitlink 未改变；工作树中的 `-` 仅表示未初始化，不表示 gitlink 变化。
5. `design/DOCUMENT_SHA256SUMS`：9/9 PASS。
6. 精确前次 A 生产证据提交中的 `SHA256SUMS`：47/47 PASS。
7. B 证据包外层：8/8 PASS；归档内原始 stdout/stderr：90/90 PASS。
8. 没有冲突解决、手工内容修改或额外文件进入候选的证据；整 tree 相同是决定性证据。

由于候选内容逐字节等于已授权来源，本轮没有重跑 19 微测试、1000+1000、四例 EQY 或
长综合；这些结果仍引用精确生产对象 `dbb21ecd5d50d5636aec7415d9a8e6adb9b5f300`
及其 A 验收 `feb137e...`，不是本轮新运行结果。

## 继承空白告警

独立执行 `git diff --check TARGET SOURCE` 返回 rc=2，得到 99 项、11 个文件，均位于来源既有
case34 历史 Markdown 或原始 Yosys 日志。独立输出 SHA-256 为：

```text
f2d8f8398ad406205922966dd31217a782c56f37b20a8ae67bd248c765ba41ae
```

它与 B 归档中的 `17.stdout` 逐字节一致。与此同时，`git diff --check SOURCE FINAL` 返回 0
且无输出，SOURCE 与 FINAL 的完整 tree 相同。因此这些是授权来源已有格式债务，不是机械
合并新增缺陷；清理它们会改变来源 tree，反而破坏本次“纯机械、整树相同”的约束，故不阻塞。

## 状态边界

- 当前已验证生产交付：case4 为 27 `$lut`。
- 历史 26 `$lut`：研究目标，证据未闭合。
- 完整历史 P2：`UNVERIFIED`。
- 外部计分合法性：`UNVERIFIED_EXTERNAL_SCORING`。
- release/eLinx：`NOT_EXECUTED`。

本结论只授权项目负责人指定范围内的机械更新，不等于完整历史 P2 达标，也不等于发布。
