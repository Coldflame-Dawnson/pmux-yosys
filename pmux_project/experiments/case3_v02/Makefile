YOSYS_CONFIG ?= yosys-config
PLUGIN := case3_v02_ablate.so

.PHONY: all clean

all: $(PLUGIN)

$(PLUGIN): case3_v02_ablate.cc
	$(YOSYS_CONFIG) --build $@ $<

clean:
	$(RM) $(PLUGIN)

