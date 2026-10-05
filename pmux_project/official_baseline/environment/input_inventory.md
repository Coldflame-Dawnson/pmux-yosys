# 官方输入清单

本清单来自逐文件读取源码及对工作区内附带文件的检索，不依据文件名猜测顶层。

| case | 采用的官方源文件（Windows 绝对路径） | 源码确认的顶层 | 文件数 | 时钟/复位/状态 | 其他依赖 |
| --- | --- | --- | ---: | --- | --- |
| case1 | `D:\Work\Embedded_competition\Original_results\pmux_case\competition_case\test1.v` | `test1` | 1 | `clock` 上升沿；`reset` 高有效异步复位；有 `initial` 初值和寄存器状态 | 无 memory 声明、include、外部模块、约束或官方脚本 |
| case2 | `D:\Work\Embedded_competition\Original_results\pmux_case\competition_case\test2.v` | `test2` | 1 | 组合逻辑；端口 `clk` 未使用；原始敏感列表没有列出 `d/datad` | 无复位、memory、include、外部模块、约束或官方脚本 |
| case3 | `D:\Work\Embedded_competition\Original_results\pmux_case\competition_case\test3.v` | `test3` | 1 | `clk` 上升沿；无复位；同文件内定义操作码宏 | 无 memory、include、外部模块、约束或官方脚本 |
| case4 | `D:\Work\Embedded_competition\Original_results\pmux_case\competition_case\test4.v` | `test4` | 1 | `clk` 上升沿；`reset_n` 低有效异步复位；复位分支未赋值 `datain_5bis7` | 无显式 memory、include、外部模块、约束或官方脚本；Yosys 会把 32 项 case 暂时推断为组合 ROM |

工作区还含有 `test1/test1.v` 至 `test4/test4.v` 四个目录内副本。每个副本与上表同名父目录文件逐字节相同，因此基线只读取上表路径，避免重复读入同一 module。

SHA-256：

- test1.v: `2D2DBA011B0E8799D0DE3790B69B7F01C6EB6AC665C4321B57B0378A4972FD13`
- test2.v: `E43C9B437071488551CAF3121E227E86F4FE3190751F245BEAC3B7ECC3FDB146`
- test3.v: `1551B9212622CCEF6019D6E77DE2C43DC8C80D267957B55A42121F34A08780E9`
- test4.v: `C0C5042928BD6BF568E2B7FB2193F2B6DB9C4193BA3A5378FC50A331D43422D7`

检索结果未发现任何随 case 附带的 README、Tcl/YS 脚本、SDC/QSF 约束、IP 文件或额外 HDL 依赖。
