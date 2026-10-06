YOSYS_CONFIG ?= yosys-config
PLUGIN := pmux_opt.so
SOURCE := pmux_opt.cc

.PHONY: all clean

all: $(PLUGIN)

$(PLUGIN): $(SOURCE) ../include/pmux_opt_api.h
	$(YOSYS_CONFIG) --build $@ $(SOURCE) -I../include

clean:
	$(RM) $(PLUGIN)
