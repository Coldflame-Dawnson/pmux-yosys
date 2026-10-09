YOSYS_ROOT ?= ../official_baseline/toolchain/install
YOSYS_CONFIG ?= $(YOSYS_ROOT)/bin/yosys-config
YOSYS ?= $(YOSYS_ROOT)/bin/yosys
PLUGIN := analyzer_selftest.so
CASE3_STAGE ?= $(YOSYS_ROOT)/../../case3/netlists/stage_proc.il
CASE3_RTL ?= $(YOSYS_ROOT)/../../../pmux_case/competition_case/test3.v
CASE4_RTL ?= $(YOSYS_ROOT)/../../../pmux_case/competition_case/test4.v
OFFICIAL_ROOT ?= /home/james/work/pmux-yosys/pmux_project/pmux_case/competition_case
PROFILE_SNAPSHOT_AUDIT_OUT ?= /tmp/a-profile-snapshot-reaudit

.PHONY: all test test-slow case3-report case34-report production-case34-report decision-cone-report profile-snapshot-reaudit clean

all: $(PLUGIN)

$(PLUGIN): readonly_analyzer.cc readonly_analyzer.h guarded_fact_analysis.h \
		branch_redundancy_analyzer.cc case34_proof_analyzer.cc budget_tracker.h selftest_plugin.cc \
		production_case34_proof_analyzer.cc branch_selftest_plugin.cc case34_selftest_plugin.cc \
		decision_cone_proof_analyzer.cc decision_cone_selftest_plugin.cc production_case34_selftest_plugin.cc case34_report_plugin.cc production_case34_report_plugin.cc decision_cone_report_plugin.cc ../include/pmux_opt_api.h \
		../include/pmux_branch_proof_api.h ../include/pmux_v02_case34_proof_api.h \
		../include/pmux_v02_production_proof_api.h ../include/pmux_v02_decision_cone_proof_api.h
	$(YOSYS_CONFIG) --build $@ -I../include -I. readonly_analyzer.cc \
		branch_redundancy_analyzer.cc case34_proof_analyzer.cc production_case34_proof_analyzer.cc decision_cone_proof_analyzer.cc \
		selftest_plugin.cc branch_selftest_plugin.cc case34_selftest_plugin.cc production_case34_selftest_plugin.cc decision_cone_selftest_plugin.cc \
		case34_report_plugin.cc production_case34_report_plugin.cc decision_cone_report_plugin.cc

test: $(PLUGIN)
	YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) -p 'help' > selftest.log
	grep -F 'PMUX analyzer selftest: all checks passed' selftest.log
	grep -F 'PMUX branch selftest: all checks passed' selftest.log
	grep -F 'PMUX case34 selftest: all checks passed' selftest.log
	grep -F 'PMUX production case34 selftest: all checks passed' selftest.log
	grep -F 'PMUX decision-cone selftest: all checks passed' selftest.log

test-slow: $(PLUGIN)
	PMUX_ANALYZER_SLOW_TESTS=1 YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) -p 'help' > selftest.log
	grep -F 'PMUX analyzer selftest: all checks passed' selftest.log
	grep -F 'PMUX branch selftest: all checks passed' selftest.log
	grep -F 'PMUX case34 selftest: all checks passed' selftest.log
	grep -F 'PMUX production case34 selftest: all checks passed' selftest.log
	grep -F 'PMUX decision-cone selftest: all checks passed' selftest.log

case3-report: $(PLUGIN)
	YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) \
		-p 'read_rtlil $(CASE3_STAGE); pmux_branch_proof_report' \
		> case3_branch_proof.log
	grep -F 'BRANCH_PROOF_NETLIST_UNCHANGED 1' case3_branch_proof.log
	grep -F 'cell=$$procmux$$15 branch=1 width=1 proven_all_bits=1 fallback=A reason=branch_redundant' case3_branch_proof.log

case34-report: $(PLUGIN)
	YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) \
		-p 'read_rtlil $(CASE3_STAGE); pmux_case34_proof_report' > case3_case34_proof.log
	grep -F 'CASE34_GUARD_REPORTS 3' case3_case34_proof.log
	grep -F 'status=0 all=1 reason=proved' case3_case34_proof.log
	grep -F 'CASE34_PROOF_NETLIST_UNCHANGED 1' case3_case34_proof.log
	YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) \
		-p 'read_verilog $(CASE4_RTL); proc; opt; memory_map; opt; pmux_case34_proof_report' \
		> case4_case34_proof.log
	grep -F 'address_width=5 output_width=10 rows=32 source_cells=31 sinks=5 status=0 complete=1 all=1 reason=proved' case4_case34_proof.log
	grep -F 'CASE34_TABLE_ROW address=0 bits=1001110000' case4_case34_proof.log
	grep -F 'CASE34_PROOF_NETLIST_UNCHANGED 1' case4_case34_proof.log

production-case34-report: $(PLUGIN)
	YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) \
		-p 'read_verilog $(CASE3_RTL); synth_intel -family cycloneiv -top test3 -run begin:map_ffram; opt -fast -mux_undef -undriven -fine -full; memory_map; opt -undriven -fine; pmux_case34_production_proof_report' \
		> production_case3_proof.log
	grep -F 'source_width=8 removed=1 comparators=3' production_case3_proof.log
	grep -F 'status=0 all_cmp=1 all_data=1 fanout=1 boundaries=1 all=1 reason=proved' production_case3_proof.log
	grep -F 'PRODUCTION_PROOF_NETLIST_UNCHANGED 1' production_case3_proof.log
	YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) \
		-p 'read_verilog $(CASE4_RTL); synth_intel -family cycloneiv -top test4 -run begin:map_ffram; opt -fast -mux_undef -undriven -fine -full; memory_map; opt -undriven -fine; pmux_case34_production_proof_report' \
		> production_case4_proof.log
	grep -F 'address_width=5 output_width=10 rows=32 source_cells=17 sinks=10 status=0 complete=1 rows_proved=1 mapped=1 all=1 reason=proved' production_case4_proof.log
	grep -F 'PRODUCTION_TABLE_ROW address=0 bits=1001110000' production_case4_proof.log
	grep -F 'PRODUCTION_PROOF_NETLIST_UNCHANGED 1' production_case4_proof.log

decision-cone-report: $(PLUGIN)
	YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) \
		-p 'read_verilog $(CASE3_RTL); synth_intel -family cycloneiv -top test3 -run begin:map_ffram; opt -fast -mux_undef -undriven -fine -full; memory_map; opt -undriven -fine; pmux_case3_decision_cone_proof_report' \
		> decision_cone_case3_proof.log
	grep -F 'source_width=8 support=7 fixed=1 comparators=3' decision_cone_case3_proof.log
	grep -F 'status=0 structure=1 guard=1 selection=1 inside=1 outside=1 sink_bits=1 fanout=1 boundaries=1 dag=1 all=1 reason=proved' decision_cone_case3_proof.log
	grep -F 'DECISION_CONE_TEST_CONSUMER reconstructed=1 constant_roots=3 shared_root_groups=2' decision_cone_case3_proof.log
	grep -F 'DECISION_CONE_NETLIST_UNCHANGED 1' decision_cone_case3_proof.log

profile-snapshot-reaudit:
	python3 profile_snapshot_reaudit.py --tool-root $(YOSYS_ROOT) \
		--official $(OFFICIAL_ROOT) --output $(PROFILE_SNAPSHOT_AUDIT_OUT) \
		> profile_snapshot_reaudit.log
	grep -F 'A_PROFILE_SNAPSHOT_SAME_NAME_REBUILD_COUNTEREXAMPLE REPRODUCED' profile_snapshot_reaudit.log

clean:
	rm -f $(PLUGIN) selftest.log case3_branch_proof.log case3_case34_proof.log case4_case34_proof.log decision_cone_case3_proof.log \
		profile_snapshot_reaudit.log \
		decision_case3.il decision_candidate.il \
		production_case3_proof.log production_case4_proof.log a_prod_case3.il a_prod_case4.il prod_case3.log prod_case4.log
