YOSYS_ROOT ?= ../official_baseline/toolchain/install
YOSYS_CONFIG ?= $(YOSYS_ROOT)/bin/yosys-config
YOSYS ?= $(YOSYS_ROOT)/bin/yosys
PLUGIN := analyzer_selftest.so

.PHONY: all test test-slow case3-report clean

all: $(PLUGIN)

$(PLUGIN): readonly_analyzer.cc readonly_analyzer.h guarded_fact_analysis.h \
		branch_redundancy_analyzer.cc budget_tracker.h selftest_plugin.cc \
		branch_selftest_plugin.cc ../include/pmux_opt_api.h ../include/pmux_branch_proof_api.h
	$(YOSYS_CONFIG) --build $@ -I../include -I. readonly_analyzer.cc \
		branch_redundancy_analyzer.cc selftest_plugin.cc branch_selftest_plugin.cc

test: $(PLUGIN)
	YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) -p 'help' > selftest.log
	grep -F 'PMUX analyzer selftest: all checks passed' selftest.log

test-slow: $(PLUGIN)
	PMUX_ANALYZER_SLOW_TESTS=1 YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) -p 'help' > selftest.log
	grep -F 'PMUX analyzer selftest: all checks passed' selftest.log

case3-report: $(PLUGIN)
	YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) \
		-p 'read_rtlil ../official_baseline/case3/netlists/stage_proc.il; pmux_branch_proof_report' \
		> case3_branch_proof.log
	grep -F 'BRANCH_PROOF_NETLIST_UNCHANGED 1' case3_branch_proof.log
	grep -F 'cell=$$procmux$$15 branch=1 width=1 proven_all_bits=1 fallback=A reason=branch_redundant' case3_branch_proof.log

clean:
	rm -f $(PLUGIN) selftest.log case3_branch_proof.log
