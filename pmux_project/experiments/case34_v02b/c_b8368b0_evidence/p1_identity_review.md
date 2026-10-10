# C P1 身份无关性扫描

结论：PASS。`p1_identity_census.json` 保存生产翻译单元、头文件的 SHA-256 和逐行身份相关引用。没有官方 case 名、固定官方端口名、固定地址零向量或 `$procmux$` 自动 cell 名匹配。静态扫描需结合以下人工分类，不能仅凭关键词零命中证明任意程序属性。

- `pmux_opt.cc` 构造 request 时记录实际 module/cell ID；筛选来源为操作类型、位宽、连接和证明结果。ID 是对象索引和 trace 元数据。
- `case34_rewrite.cc` 使用 request/proof 的实际 ID 找回同一设计对象、验证 proof 与 rewrite 一致，处理 claimed cell 冲突；不是按官方身份启用规则。1280–1410 行遍历 selected modules 并调用两个结构证明 analyzer，生成、验证和应用计划。
- `rewrite_plan.cc` 的名字、指针和连接检查用于提交前定位已证明目标，防止对象变化。
- `pmux_profile_authorization.h` 的 M/C 名字、端口名、参数和属性构成完整快照；Design 指针是 monitor registry 的归属键，generation 绑定存活 monitor。它们限制已发布授权的消费，不能产生新的结构证明授权。58 个独立夹具覆盖名字/内容相同但对象已重建的拒绝。
- Analyzer 的 module_id/cell_id 相等检查是 request 合法性和 proof graph 定位，名字排序用于确定性。`cofactor_key` 根据实际 address/support/cofactor LUT 结构计算，不比较官方固定向量。
- `synth_intel.cc` 的字符串分割用于 `-run` 参数阶段解析；profile 消费调用统一授权 helper。runner 也调用同一 helper。两条消费者的 29 个逐项 profile/reason 完全一致。

P1 的范围是结构候选发现和选择不按设计身份分派。授权快照保留对象身份以拒绝重放，属于必须验证的生命周期边界，不构成按 case 身份优化。
