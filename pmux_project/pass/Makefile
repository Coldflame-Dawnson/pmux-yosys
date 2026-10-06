YOSYS_CONFIG ?= yosys-config
PLUGIN := pmux_opt.so
SOURCES := pmux_opt.cc rewrite_plan.cc ../analyzer/readonly_analyzer.cc

.PHONY: all clean

all: $(PLUGIN)

$(PLUGIN): $(SOURCES) rewrite_plan.h ../analyzer/readonly_analyzer.h ../analyzer/budget_tracker.h ../include/pmux_opt_api.h
	$(YOSYS_CONFIG) --build $@ $(SOURCES) -I../include -I../analyzer

clean:
	$(RM) $(PLUGIN)
