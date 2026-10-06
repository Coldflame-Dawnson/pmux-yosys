# 已解决的联调运行器差异

早期两次运行失败均已保留证据；目前未发现未解决的 B 改写失败。

1. 首次 case3 使用了 SHA-256 为 `9d78...` 的另一份 checkpoint，B 自带测试比较整份 JSON 时因辅助 vendor 模块缺失失败。目标 `test3` 模块本身一致。改用冻结 `baa2...` 插入点输入后，16/16 PASS。
2. 首次 C 定向 22 例中，第 6 类的网表、check、EQY 均通过；C 将 B 的 `new_type=connection`、`transformation=bypass_a` 错误预期成 `null`、`bypass_to_a`。修正 C 的机器字段断言后，22/22 PASS。

原失败输入、before/after、plan/stats、EQY 和日志见 `evidence.tar.gz`。本诊断不表示生产证明路径已验证。
