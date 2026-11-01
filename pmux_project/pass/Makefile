YOSYS_CONFIG ?= yosys-config
YOSYS ?= yosys
PLUGIN := pmux_opt.so
SOURCES := pmux_opt.cc rewrite_plan.cc case12_rewrite.cc case34_rewrite.cc \
	../analyzer/readonly_analyzer.cc ../analyzer/case34_proof_analyzer.cc \
	../analyzer/case12_proof_analyzer.cc \
	../analyzer/production_case34_proof_analyzer.cc \
	../analyzer/decision_cone_proof_analyzer.cc

.PHONY: all test-case12 clean

all: $(PLUGIN)

$(PLUGIN): $(SOURCES) rewrite_plan.h case12_rewrite.h case34_rewrite.h pmux_profile_authorization.h \
		../analyzer/readonly_analyzer.h ../analyzer/budget_tracker.h \
		../analyzer/guarded_fact_analysis.h ../include/pmux_opt_api.h \
		../include/pmux_v02_case12_proof_api.h \
		../include/pmux_v02_case34_proof_api.h ../include/pmux_v02_production_proof_api.h \
		../include/pmux_v02_decision_cone_proof_api.h
	$(YOSYS_CONFIG) --build $@ $(SOURCES) -I../include -I../analyzer

test-case12: $(PLUGIN)
	python3 ../experiments/case12_v02/scripts/run_case12_validation.py \
		--repo ../.. --yosys $(YOSYS)

clean:
	$(RM) $(PLUGIN)
