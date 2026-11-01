#include "kernel/yosys.h"
#include "../../../../pmux_project/pass/pmux_profile_authorization.h"

#include <algorithm>
#include <string>
#include <vector>

USING_YOSYS_NAMESPACE

namespace {

Pass *production_pmux_opt = nullptr;

struct BindProductionPmuxOpt
{
	BindProductionPmuxOpt()
	{
		production_pmux_opt = pass_register.at("pmux_opt");
		pass_register.erase("pmux_opt");
	}
} bind_production_pmux_opt;

RTLIL::Cell *first_generated_cell(RTLIL::Design *design)
{
	std::vector<RTLIL::Cell *> cells;
	for (RTLIL::Module *module : design->modules())
		for (RTLIL::Cell *cell : module->cells())
			if (cell->type == ID(pmux_case34_lut4))
				cells.push_back(cell);
	std::sort(cells.begin(), cells.end(), [](const RTLIL::Cell *lhs, const RTLIL::Cell *rhs) {
		return lhs->name.str() < rhs->name.str();
	});
	return cells.empty() ? nullptr : cells.front();
}

struct ProfileSnapshotMutationPass : Pass
{
	ProfileSnapshotMutationPass() : Pass("pmux_opt", "test-only PMUX profile snapshot mutation seam") { }

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		production_pmux_opt->execute(args, design);
		const std::string mode = design->scratchpad_get_string("pmux_test.profile_mutation", "none");
		if (mode == "none")
			return;
		if (mode == "drop_snapshot") {
			design->scratchpad_set_string("pmux_opt.accepted_constant_table_snapshot", std::string());
			log("PMUX_PROFILE_MUTATION mode=%s\n", mode.c_str());
			return;
		}
		if (mode == "forged_generation") {
			design->scratchpad_set_string(
					PmuxProfileAuthorization::generation_key, "B-forged-generation");
			log("PMUX_PROFILE_MUTATION mode=%s lifecycle_monitor_match=%d\n",
					mode.c_str(),
					PmuxProfileAuthorization::constant_table_lifecycle_matches(design));
			return;
		}
		if (mode == "unregister_monitor") {
			const auto *monitor = PmuxProfileAuthorization::registered_lifecycle_monitor(design);
			if (monitor == nullptr)
				log_cmd_error("PMUX lifecycle test found no registered monitor.\n");
			design->monitors.erase(const_cast<PmuxProfileAuthorization::LifecycleMonitor *>(monitor));
			log("PMUX_PROFILE_MUTATION mode=%s lifecycle_monitor_match=%d\n",
					mode.c_str(),
					PmuxProfileAuthorization::constant_table_lifecycle_matches(design));
			return;
		}

		RTLIL::Cell *cell = first_generated_cell(design);
		if (cell == nullptr)
			log_cmd_error("pmux profile mutation seam found no generated LUT cell.\n");
		if (mode == "lut_bit") {
			std::vector<RTLIL::State> bits = cell->getParam(ID(LUT)).to_bits();
			bits.at(0) = bits.at(0) == RTLIL::State::S1 ? RTLIL::State::S0 : RTLIL::State::S1;
			cell->setParam(ID(LUT), RTLIL::Const(bits));
		} else if (mode == "lut_width") {
			std::vector<RTLIL::State> bits = cell->getParam(ID(LUT)).to_bits();
			bits.push_back(RTLIL::State::S0);
			cell->setParam(ID(LUT), RTLIL::Const(bits));
		} else if (mode == "port") {
			RTLIL::SigSpec input = cell->getPort(ID::A);
			input[0] = RTLIL::State::S0;
			cell->setPort(ID::A, input);
		} else if (mode == "attribute") {
			cell->set_bool_attribute(ID(pmux_test_changed));
		} else if (mode == "type") {
			cell->type = ID($lut);
			cell->setParam(ID::WIDTH, 4);
		} else if (mode == "remove_one") {
			cell->module->remove(cell);
		} else if (mode == "add_extra") {
			RTLIL::Module *module = cell->module;
			RTLIL::Wire *wire = module->addWire(NEW_ID);
			RTLIL::Cell *extra = module->addCell(NEW_ID, ID(pmux_case34_lut4));
			extra->setParam(ID(LUT), RTLIL::Const(0, 16));
			extra->setPort(ID::A, RTLIL::SigSpec(RTLIL::State::S0, 4));
			extra->setPort(ID::Y, wire);
			extra->set_bool_attribute(ID::keep);
		} else if (mode == "same_name_rebuild") {
			RTLIL::Module *module = cell->module;
			const RTLIL::IdString name = cell->name;
			const RTLIL::IdString type = cell->type;
			const auto parameters = cell->parameters;
			const auto attributes = cell->attributes;
			const auto connections = cell->connections();
			const auto before = PmuxProfileAuthorization::build_constant_table_snapshot(design);
			module->remove(cell);
			RTLIL::Cell *replacement = module->addCell(name, type);
			replacement->parameters = parameters;
			replacement->attributes = attributes;
			for (const auto &connection : connections)
				replacement->setPort(connection.first, connection.second);
			const auto after = PmuxProfileAuthorization::build_constant_table_snapshot(design);
			log("PMUX_PROFILE_SAME_NAME_REBUILD payload_equal=%d lifecycle_valid=%d\n",
					before.payload == after.payload,
					design->scratchpad_get_bool(
							PmuxProfileAuthorization::lifecycle_key, false));
		} else {
			log_cmd_error("unknown PMUX profile mutation mode %s.\n", mode.c_str());
		}
		log("PMUX_PROFILE_MUTATION mode=%s\n", mode.c_str());
	}
} profile_snapshot_mutation_pass;

struct AddReplayCellPass : Pass
{
	AddReplayCellPass() : Pass("pmux_test_add_lut4", "add a test-only generated LUT for replay checks") { }

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		extra_args(args, 1, design);
		std::vector<RTLIL::Module *> modules = design->selected_modules();
		if (modules.size() != 1)
			log_cmd_error("pmux_test_add_lut4 requires exactly one selected module.\n");
		RTLIL::Module *module = modules.front();
		RTLIL::Wire *wire = module->addWire(NEW_ID);
		RTLIL::Cell *cell = module->addCell(NEW_ID, ID(pmux_case34_lut4));
		cell->setParam(ID(LUT), RTLIL::Const(0x6996, 16));
		cell->setPort(ID::A, RTLIL::SigSpec(RTLIL::State::S0, 4));
		cell->setPort(ID::Y, wire);
		cell->set_bool_attribute(ID::keep);
		log("PMUX_PROFILE_REPLAY_CELL_ADDED cell=%s\n", log_id(cell));
	}
} add_replay_cell_pass;

} // namespace
