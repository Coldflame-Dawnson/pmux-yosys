YOSYS_ROOT ?= ../official_baseline/toolchain/install
YOSYS_CONFIG ?= $(YOSYS_ROOT)/bin/yosys-config
YOSYS ?= $(YOSYS_ROOT)/bin/yosys
PLUGIN := analyzer_selftest.so
CASE3_STAGE ?= $(YOSYS_ROOT)/../../case3/netlists/stage_proc.il
CASE4_RTL ?= $(YOSYS_ROOT)/../../../pmux_case/competition_case/test4.v

.PHONY: all test test-slow case3-report case34-report clean

all: $(PLUGIN)

$(PLUGIN): readonly_analyzer.cc readonly_analyzer.h guarded_fact_analysis.h \
		branch_redundancy_analyzer.cc case34_proof_analyzer.cc budget_tracker.h selftest_plugin.cc \
		branch_selftest_plugin.cc case34_selftest_plugin.cc case34_report_plugin.cc ../include/pmux_opt_api.h \
		../include/pmux_branch_proof_api.h ../include/pmux_v02_case34_proof_api.h
	$(YOSYS_CONFIG) --build $@ -I../include -I. readonly_analyzer.cc \
		branch_redundancy_analyzer.cc case34_proof_analyzer.cc selftest_plugin.cc \
		branch_selftest_plugin.cc case34_selftest_plugin.cc case34_report_plugin.cc

test: $(PLUGIN)
	YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) -p 'help' > selftest.log
	grep -F 'PMUX analyzer selftest: all checks passed' selftest.log
	grep -F 'PMUX branch selftest: all checks passed' selftest.log
	grep -F 'PMUX case34 selftest: all checks passed' selftest.log

test-slow: $(PLUGIN)
	PMUX_ANALYZER_SLOW_TESTS=1 YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) -p 'help' > selftest.log
	grep -F 'PMUX analyzer selftest: all checks passed' selftest.log
	grep -F 'PMUX branch selftest: all checks passed' selftest.log
	grep -F 'PMUX case34 selftest: all checks passed' selftest.log

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

clean:
	rm -f $(PLUGIN) selftest.log case3_branch_proof.log case3_case34_proof.log case4_case34_proof.log
