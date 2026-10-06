YOSYS_CONFIG ?= yosys-config
PLUGIN := pmux_opt.so
SOURCES := pmux_opt.cc rewrite_plan.cc

.PHONY: all clean

all: $(PLUGIN)

$(PLUGIN): $(SOURCES) rewrite_plan.h ../include/pmux_opt_api.h
	$(YOSYS_CONFIG) --build $@ $(SOURCES) -I../include

clean:
	$(RM) $(PLUGIN)
