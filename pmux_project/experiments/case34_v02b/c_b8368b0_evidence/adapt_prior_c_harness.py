from pathlib import Path
E=Path('/home/yuewen/work/pmux-yosys-c-b8368b0/pmux_project/experiments/case34_v02b/c_b8368b0_evidence')
old=Path('/home/yuewen/work/pmux-yosys-c-621cb4e/pmux_project/experiments/case34_v02b/c_621cb4e_evidence')
for n in ['build_c.py','native_acceptance.py','dut_adapter.py','regress_c.py']:
 s=(old/n).read_text().replace('621cb4e56b734da73cfb6161b951a375893e2d81','b8368b0f3e2d55a2bb429296a3aea859eb4f2049').replace('9107a466f74ce27269361ee26e99093c1b8fc93e','ec86209a61f22020707b299f5175f02b8d71495d').replace('621cb4e','b8368b0')
 if n=='native_acceptance.py':
  s=s.replace("return 'accepted_constant_table' if 'techmap -map +/intel/common/pmux_case34_lut_map.v' in log or 'pmux_case34_abc.script' in log else 'stock'","return re.findall(r'synth_intel: pmux_profile=(\\w+)',log)[0]")
  s=s.replace("'scratchpad -assert pmux_opt.accepted_constant_table false','stat'","'scratchpad -assert pmux_opt.accepted_constant_table false','scratchpad -assert pmux_opt.accepted_constant_table_snapshot \\\"\\\"','scratchpad -assert pmux_opt.accepted_constant_table_generation \\\"\\\"','scratchpad -assert pmux_opt.accepted_constant_table_lifecycle_valid false','check -noinit','stat'")
 (E/n).write_text(s)