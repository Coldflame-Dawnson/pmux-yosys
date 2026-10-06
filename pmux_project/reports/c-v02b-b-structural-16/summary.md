# B 自带 v0.2-B 结构用例独立运行

在 `integration/c-v02b-b-contract` cherry-pick B 提交 `bff595118a130756f504af99c09000c98adda4ba` 后，使用冻结 Yosys 0.69 编译 `pmux_opt.so`，运行 B 的 `run_branch_delete_tests.py`。冻结 case3 插入点输入 SHA-256 为 `baa2a34eb6fd25db3950defbc141a1477d7114b38eee971d9b02cca12d84af1e`，预期 JSON SHA-256 为 `3013f07a9202646a85b7dd1f9e8d0f6dd6d132d75c1009e6a5f9aa7895db0266`。

结果：16/16 PASS。各用例 before、after JSON、plan、stats 和完整日志见 `evidence.tar.gz`。本套使用 test-only certificate，不能证明 A+B 生产路径正确。
