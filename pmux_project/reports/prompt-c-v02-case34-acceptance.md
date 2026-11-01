# 发给 C 的提示词

你是 PMUX 优化项目的 C：独立验证与评测负责人。请独立复核 B 的
case3/case4 v0.2 算法证据；不要修改 A 的分析器、B 的 `pass/`、官方 RTL
或测试判据。

材料：

```text
B 分支：feature/b-v02-case34
B 起点：3aff789a3badf6fecc4d3651e4b7eea52627fe3c
设计记录：pmux_project/reports/b-v02-case34-start.md
实验包：pmux_project/experiments/case34_v02b/
固定官方输入：pmux_project/pmux_case/competition_case/test3.v、test4.v
```

请执行：

1. 先校验全部 `SHA256SUMS` 和官方输入哈希：
   - test3：`1551B9212622CCEF6019D6E77DE2C43DC8C80D267957B55A42121F34A08780E9`
   - test4：`C0C5042928BD6BF568E2B7FB2193F2B6DB9C4193BA3A5378FC50A331D43422D7`
2. 从官方 RTL独立重跑基线，不从中间 IL 续跑：case3=13 `$lut`、17 DFF；
   case4=36 `$lut`、10 DFF。
3. 运行包内短重放，核对 case3：11 `$lut`、12 顺序位、IO 不增加，
   source EQY PASS、官方 RTL对 post-ABC9 gate EQY PASS。
4. 核对 case4 自动提取的 32×10 表，逐地址比较官方 RHS；任一差异立即
   FAIL。确认地址 0 为 `1001110000`。
5. 重放固定向量 `[3,3,1,4,4,4,0,2,4,4]`，并核对：
   - 严格可比 post-ABC9：27 `$lut`、10 顺序位；
   - 厂商映射：26 `fiftyfivenm_lcell_comb` 加 1 `$not`；
   - whole-design gate EQY PASS。
6. 审查 544 候选的固定种子 trace，确认最好 post-ABC9 是 27，不得把
   厂商 26 原语写成 26 `$lut`。
7. 独立判断 v0.2 case4 oracle 是否应从历史 26 `$lut` 更正为：
   `27 post-ABC9 $lut / 26 vendor lcell + 1 $not`。若不同意，必须提供
   同一完整阶段下的 26 `$lut` 网表、命令和 EQY PASS。
8. A/B 集成后再运行新规则微测试、1000 固定种子和四官方 case；当前
   实验候选 PASS 不得自动继承为生产 Pass PASS。

保存：完整命令、工具版本/提交/可执行哈希、阶段 IL/JSON、日志、EQY
配置/工作目录/PASS、CPU/ABC 时间、峰值内存和独立 `SHA256SUMS`。

最终返回：分支、起点、提交哈希、逐项 PASS/FAIL、case3/case4 双口径
计数、whole-design EQY、对 case4 oracle 的书面结论、发现的差异、工作树
状态，并明确是否允许 B 使用该验收结论。
