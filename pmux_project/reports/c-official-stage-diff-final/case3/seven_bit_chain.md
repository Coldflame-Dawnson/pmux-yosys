# case3 七位扇出与删除链

`opt_clean` 删除 cell：无；新增 cell：无。
首次结构重新相同：未出现；首次统计重新相同：05_post_abc9。

| 目标物理位 | Branch | 改写前 → 后 | 驱动 cell | 改写前扇出 | opt_clean 后扇出 | 被删除的扇出 cell |
|---|---|---|---|---|---|---|
| $procmux$10.B[0] | None | sr[2] → 1 | $auto$ff.cc:337:slice$44.Q[2] | $auto$ff.cc:337:slice$44.D[3], $procmux$10.B[0], $procmux$20_CMP0.A[2], $procmux$27.A[1], $procmux$28_CMP0.A[2], $procmux$29_CMP0.A[2] | $auto$ff.cc:337:slice$44.D[3], $procmux$20_CMP0.A[2], $procmux$27.A[1], $procmux$28_CMP0.A[2], $procmux$29_CMP0.A[2] | 无 |
| $procmux$10.B[1] | None | sr[1] → 1 | $auto$ff.cc:337:slice$44.Q[1] | $auto$ff.cc:337:slice$44.D[2], $procmux$10.B[1], $procmux$20_CMP0.A[1], $procmux$27.A[2], $procmux$28_CMP0.A[1], $procmux$29_CMP0.A[1] | $auto$ff.cc:337:slice$44.D[2], $procmux$20_CMP0.A[1], $procmux$27.A[2], $procmux$28_CMP0.A[1], $procmux$29_CMP0.A[1] | 无 |
| $procmux$10.B[2] | None | sr[0] → 1 | $auto$ff.cc:337:slice$44.Q[0] | $auto$ff.cc:337:slice$44.D[1], $procmux$10.B[2], $procmux$18.A[0], $procmux$20_CMP0.A[0], $procmux$27.A[3], $procmux$28_CMP0.A[0], $procmux$29_CMP0.A[0] | $auto$ff.cc:337:slice$44.D[1], $procmux$18.A[0], $procmux$20_CMP0.A[0], $procmux$27.A[3], $procmux$28_CMP0.A[0], $procmux$29_CMP0.A[0] | 无 |
| $procmux$18.B[0] | 0 | sr[3] → 1 | $auto$ff.cc:337:slice$44.Q[3] | $auto$ff.cc:337:slice$44.D[4], $auto$ff.cc:337:slice$62.D[0], $procmux$18.B[0], $procmux$20_CMP0.A[3], $procmux$27.A[0], $procmux$28_CMP0.A[3], $procmux$29_CMP0.A[3] | $auto$ff.cc:337:slice$44.D[4], $auto$ff.cc:337:slice$62.D[0], $procmux$20_CMP0.A[3], $procmux$27.A[0], $procmux$28_CMP0.A[3], $procmux$29_CMP0.A[3] | 无 |
| $procmux$27.B[1] | None | sr[6] → 1 | $auto$ff.cc:337:slice$44.Q[6] | $auto$ff.cc:337:slice$44.D[7], $procmux$10.A[0], $procmux$20_CMP0.A[6], $procmux$27.B[1], $procmux$28_CMP0.A[6], $procmux$29_CMP0.A[6] | $auto$ff.cc:337:slice$44.D[7], $procmux$10.A[0], $procmux$20_CMP0.A[6], $procmux$28_CMP0.A[6], $procmux$29_CMP0.A[6] | 无 |
| $procmux$27.B[2] | None | sr[5] → 1 | $auto$ff.cc:337:slice$44.Q[5] | $auto$ff.cc:337:slice$44.D[6], $procmux$10.A[1], $procmux$20_CMP0.A[5], $procmux$27.B[2], $procmux$28_CMP0.A[5], $procmux$29_CMP0.A[5] | $auto$ff.cc:337:slice$44.D[6], $procmux$10.A[1], $procmux$20_CMP0.A[5], $procmux$28_CMP0.A[5], $procmux$29_CMP0.A[5] | 无 |
| $procmux$27.B[3] | None | sr[4] → 1 | $auto$ff.cc:337:slice$44.Q[4] | $auto$ff.cc:337:slice$44.D[5], $procmux$10.A[2], $procmux$20_CMP0.A[4], $procmux$27.B[3], $procmux$28_CMP0.A[4], $procmux$29_CMP0.A[4] | $auto$ff.cc:337:slice$44.D[5], $procmux$10.A[2], $procmux$20_CMP0.A[4], $procmux$28_CMP0.A[4], $procmux$29_CMP0.A[4] | 无 |

七个位原信号仍有目标以外的扇出；此次位改写后 `opt_clean` 未删除任何 cell。ABC9 前少 1 个 AND、1 个 OR，ABC9 后均为 13 LUT。
