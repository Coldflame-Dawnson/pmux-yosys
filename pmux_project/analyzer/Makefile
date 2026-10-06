YOSYS_ROOT ?= ../official_baseline/toolchain/install
YOSYS_CONFIG ?= $(YOSYS_ROOT)/bin/yosys-config
YOSYS ?= $(YOSYS_ROOT)/bin/yosys
PLUGIN := analyzer_selftest.so

.PHONY: all test test-slow clean

all: $(PLUGIN)

$(PLUGIN): readonly_analyzer.cc readonly_analyzer.h budget_tracker.h selftest_plugin.cc ../include/pmux_opt_api.h
	$(YOSYS_CONFIG) --build $@ -I../include -I. readonly_analyzer.cc selftest_plugin.cc

test: $(PLUGIN)
	YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) -p 'help' > selftest.log
	grep -F 'PMUX analyzer selftest: all checks passed' selftest.log

test-slow: $(PLUGIN)
	PMUX_ANALYZER_SLOW_TESTS=1 YOSYS_DATDIR=$(YOSYS_ROOT)/share/yosys $(YOSYS) -Q -m ./$(PLUGIN) -p 'help' > selftest.log
	grep -F 'PMUX analyzer selftest: all checks passed' selftest.log

clean:
	rm -f $(PLUGIN) selftest.log
