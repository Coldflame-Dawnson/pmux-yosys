# case3 B test-only certificate

结果：PASS_TEST_ONLY；机器计划：PASS；目标模块结构精确相同：True。

基线 {'lut': 13, 'dff': 17, 'io': 0}，test-only certificate {'lut': 12, 'dff': 17, 'io': 0}；EQY=PASS，方法=whole。

A 真实证明提供者未接入，13→12 LUT 只能归于 test-only 结构路径，不得计作生产收益。
