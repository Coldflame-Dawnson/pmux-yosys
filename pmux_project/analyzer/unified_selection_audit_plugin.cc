#include "kernel/yosys.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct PmuxAuditReintroduceLut4Pass : public Pass {
	PmuxAuditReintroduceLut4Pass() : Pass("pmux_audit_reintroduce_lut4",
			"A-only fixture that reintroduces one marked table cell") { }

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		extra_args(args, 1, design);
		RTLIL::Module *module = design->top_module();
		if (module == nullptr)
			log_cmd_error("pmux_audit_reintroduce_lut4: top module is required.\n");
		RTLIL::Wire *input = module->addWire(NEW_ID, 4);
		RTLIL::Wire *output = module->addWire(NEW_ID);
		RTLIL::Cell *cell = module->addCell(NEW_ID, ID(pmux_case34_lut4));
		cell->setParam(ID(LUT), RTLIL::Const(0x6996, 16));
		cell->setPort(ID(A), RTLIL::SigSpec(input));
		cell->setPort(ID(Y), RTLIL::SigSpec(output));
		cell->set_bool_attribute(ID::keep);
		log("PMUX_AUDIT_REINTRODUCED_LUT4 module=%s cell=%s\n",
				log_id(module), log_id(cell));
	}
} PmuxAuditReintroduceLut4Pass;

PRIVATE_NAMESPACE_END
