# case3/case4 v0.2-B 算法设计与短验证结果

日期：2026-10-07
角色：B（case3/case4 算法材料；生产 Pass/集成）
证据状态：本文件中的计数与 PASS 均为本机实际执行结果；C 已独立重放并确认
case3/case4 计数、两级 EQY 和 544 条搜索轨迹。2026-10-07 又使用当前已提交
候选重新生成证据，以修正 case3 source 哈希的溯源差异。

## 1. 不变量与口径

1. gold 是官方 `test3.v`/`test4.v`，没有提交官方 RTL 副本。
2. baseline 和 candidate 使用同一固定 Yosys 0.69 构建。
3. “post-ABC9 `$lut`”只统计相应 JSON 顶层 module 中 `type == "$lut"`。
4. “厂商组合原语”单独统计 `fiftyfivenm_lcell_comb`，不得与 `$lut` 混写。
5. source EQY 与“官方 RTL 对实际 post-ABC9 RTLIL”两级 EQY 均必须 PASS。
6. 任何生产规则仍须经 A 的证明结果授权和 C 的独立验收；本证据不授权按
   case 身份匹配或把固定向量写入运行时。

官方输入 SHA-256：

```text
test3.v 1551B9212622CCEF6019D6E77DE2C43DC8C80D267957B55A42121F34A08780E9
test4.v C0C5042928BD6BF568E2B7FB2193F2B6DB9C4193BA3A5378FC50A331D43422D7
```

## 2. case3：guard 下比较器缩宽与数据折叠

### 2.1 结构规则

外层当前周期 guard 为 `cs && sr[7]`。仅在该 guard 支配的选择锥内使用事实
`sr[7] = 1`：

```text
sr == 8'hAA  <=> sr[6:0] == 7'h2A，选中数据为 8'h55
sr == 8'hFF  <=> sr[6:0] == 7'h7F，选中低四位为 4'hF
sr == 8'hFE  <=> sr[6:0] == 7'h7E，选中高四位为 4'hF
default 保留 gpioout[0] <= sr[0]
```

候选保持两个独立 `if` 的当前周期语义、`sr` 的移位、`sr_out` 的保持，以及
`gpioout` 未更新位的保持。生产匹配必须从结构上的 guard 支配关系、比较端口
和选择数据推导这些事实，不能匹配 `test3`、`sr` 或上述常量名称。

### 2.2 实测

| 指标 | baseline | candidate |
|---|---:|---:|
| post-ABC9 `$lut` | 13 | 11 |
| post-ABC9 `dffeas` | 17 | 12 |
| 顺序位 | 17 | 12 |
| 厂商 `fiftyfivenm_lcell_comb` | — | 10 |
| 厂商 `$not` | — | 1 |
| source whole-design EQY | — | PASS |
| official RTL 对 post-ABC9 gate EQY | — | PASS |

顺序位减少来自等价综合合并，不是跨寄存器传播；候选没有增加 DFF 或 IO。
candidate source SHA-256：
`DD2EB7F827A7B8719914E972D054D12925D2109F4EDE33C65293BE69B887EC52`。
post-ABC9 gate SHA-256：
`BB1026ED444F44A6D8859BB9A180186793B7A404E080DC5BDA0D062405A5954D`。

## 3. case4：有界常量表 Shannon/cofactor 分解

### 3.1 结构规则

生成器解析官方拼接：

```text
{dataout[5:0], only_5b, datain_5bis7, data_dp, data_dn}
```

并逐地址恢复 32×10 真值表。地址 0 的正确输出词是 `10'b1001110000`。
对每个输出位选择一个地址位作 Shannon split：

```text
f = x ? f1 : f0
cofactor key = (remaining_support, 16-bit truth mask)
```

常量 cofactor 全局折叠；相同 key 跨输出共享。搜索使用固定种子 `20261006`、
500 个偏置随机候选、5 个统一 split、历史向量和严格改善邻域；共评估 544 个
唯一向量。最好向量为 `[3,3,1,4,4,4,0,2,4,4]`，第一轮邻域没有严格改善。
该向量只属于证据和验收 oracle；生产算法必须由当前真值表在预算内重新搜索，
不得按 case 身份直接套用。

### 3.2 实测与口径更正

| 指标 | baseline | candidate |
|---|---:|---:|
| post-ABC9 `$lut` | 36 | 27 |
| 顺序位 / `dffeas` | 10 | 10 |
| 厂商 `fiftyfivenm_lcell_comb` | 35 | 26 |
| 厂商 `$not` | 1 | 1 |
| source whole-design EQY | — | PASS |
| official RTL 对 post-ABC9 gate EQY | — | PASS |

历史“26 `$lut`”只在未执行公共 FF/techmap、仍保留 `$adff/$sdffce` 的非严格
截点上出现，不能与官方完整流程的 36 `$lut` 比较。严格流程的第 27 个 `$lut`
是驱动 `only_5b` 的一输入反相器；厂商映射把它恢复为 `$not`，所以 **27 个
post-ABC9 `$lut`** 与 **26 个厂商组合原语加 1 个 `$not`** 同时成立。

candidate source SHA-256：
`C789DB300EC3FC5D4E60F0C2F6698D39E4836D7DA7FD05D328FB3BE17AC4DEB4`。
post-ABC9 gate SHA-256：以 `evidence/summary.json` 的当前生成值为准；本次刷新为
`BD9D87E50E510E2EB1C12896D1FABA327104ADDA741E01DF4C084B202BE8EFC5`。

## 4. 生产化门禁

当前只完成算法设计、候选生成和短验证。B 的生产结构改写保持未启用，直到：

1. A 的接口与实验拓扑证明已完成；仍须补齐冻结 `synth_intel` 插入点的
   case3 guard 证明和 case4 完整 5×10 表/sequential sink 证明；
2. C 已独立核对真值表、统计停止点、EQY gold/gate 和 PASS；
3. 团队决定把 case4 的 P2 严格 oracle 修正为 27 `$lut`，或提供同阶段 26
   `$lut` 的新证据；
4. B 的结构 matcher、快照预检、隔离试改、成本门和原子提交通过负例测试；
5. 四个官方 case 的统一流程全部 EQY PASS，且不低于经确认的 P2 oracle。

因此本提交不会修改 `synth_intel` 默认流程，也不会把候选常量或真值表硬编码
到生产 Pass。
