YOSYS_CONFIG ?= yosys-config
PLUGIN := pmux_opt.so
SOURCES := pmux_opt.cc rewrite_plan.cc case34_rewrite.cc \
	../analyzer/readonly_analyzer.cc ../analyzer/case34_proof_analyzer.cc \
	../analyzer/production_case34_proof_analyzer.cc \
	../analyzer/decision_cone_proof_analyzer.cc

.PHONY: all clean

all: $(PLUGIN)

$(PLUGIN): $(SOURCES) rewrite_plan.h case34_rewrite.h \
		../analyzer/readonly_analyzer.h ../analyzer/budget_tracker.h \
		../analyzer/guarded_fact_analysis.h ../include/pmux_opt_api.h \
		../include/pmux_v02_case34_proof_api.h ../include/pmux_v02_production_proof_api.h \
		../include/pmux_v02_decision_cone_proof_api.h
	$(YOSYS_CONFIG) --build $@ $(SOURCES) -I../include -I../analyzer

clean:
	$(RM) $(PLUGIN)
