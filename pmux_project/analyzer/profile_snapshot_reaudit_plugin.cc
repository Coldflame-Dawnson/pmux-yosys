#include "kernel/yosys.h"
#include "../pass/pmux_profile_authorization.h"

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

USING_YOSYS_NAMESPACE

namespace {

Pass *production_pmux_opt = nullptr;

struct BindProductionPmuxOptForSnapshotReaudit
{
	BindProductionPmuxOptForSnapshotReaudit()
	{
		production_pmux_opt = pass_register.at("pmux_opt");
		pass_register.erase("pmux_opt");
	}
} bind_production_pmux_opt_for_snapshot_reaudit;

RTLIL::Cell *first_target(RTLIL::Design *design)
{
	std::vector<std::tuple<std::string, std::string, RTLIL::Cell *>> targets;
	for (RTLIL::Module *module : design->modules())
		for (RTLIL::Cell *cell : module->cells())
			if (cell->type == ID(pmux_case34_lut4))
				targets.emplace_back(module->name.str(), cell->name.str(), cell);
	std::sort(targets.begin(), targets.end());
	return targets.empty() ? nullptr : std::get<2>(targets.front());
}

void rebuild_same_name_and_contents(RTLIL::Design *design)
{
	RTLIL::Cell *old_cell = first_target(design);
	if (old_cell == nullptr)
		log_cmd_error("snapshot reaudit found no target cell.\n");

	RTLIL::Module *module = old_cell->module;
	const RTLIL::IdString name = old_cell->name;
	const RTLIL::IdString type = old_cell->type;
	const auto parameters = old_cell->parameters;
	const auto attributes = old_cell->attributes;
	const auto connections = old_cell->connections();
	const auto before = PmuxProfileAuthorization::build_constant_table_snapshot(design);

	module->remove(old_cell);
	RTLIL::Cell *new_cell = module->addCell(name, type);
	new_cell->parameters = parameters;
	new_cell->attributes = attributes;
	for (const auto &connection : connections)
		new_cell->setPort(connection.first, connection.second);

	const auto after = PmuxProfileAuthorization::build_constant_table_snapshot(design);
	log("A_PROFILE_SNAPSHOT_REBUILD module=%s cell=%s before_cells=%d after_cells=%d payload_equal=%d lifecycle_valid=%d\n",
			log_id(module), log_id(new_cell), before.cell_count, after.cell_count,
			before.payload == after.payload,
			design->scratchpad_get_bool(PmuxProfileAuthorization::lifecycle_key, false));
}

void change_and_restore_port(RTLIL::Design *design)
{
	RTLIL::Cell *cell = first_target(design);
	if (cell == nullptr)
		log_cmd_error("lifecycle reaudit found no target cell.\n");
	RTLIL::SigSpec original = cell->getPort(ID::A);
	RTLIL::SigSpec changed = original;
	changed[0] = changed[0] == RTLIL::SigBit(RTLIL::State::S0) ?
			RTLIL::SigBit(RTLIL::State::S1) : RTLIL::SigBit(RTLIL::State::S0);
	cell->setPort(ID::A, changed);
	cell->setPort(ID::A, original);
	log("A_PROFILE_PORT_RESTORED payload_equal=1 lifecycle_valid=%d\n",
			design->scratchpad_get_bool(PmuxProfileAuthorization::lifecycle_key, false));
}

struct SnapshotReauditPmuxWrapper : Pass
{
	SnapshotReauditPmuxWrapper() : Pass("pmux_opt", "A-only lifecycle binding reaudit wrapper") { }

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		production_pmux_opt->execute(args, design);
		const std::string mode = design->scratchpad_get_string(
				"pmux_audit.profile_snapshot_mode", "none");
		if (mode == "none")
			return;
		if (mode == "same_name_rebuild") {
			rebuild_same_name_and_contents(design);
			return;
		}
		if (mode == "port_restore") {
			change_and_restore_port(design);
			return;
		}
		if (mode == "forge_generation") {
			design->scratchpad_set_string(PmuxProfileAuthorization::generation_key,
					"A-forged-generation");
			log("A_PROFILE_GENERATION_FORGED lifecycle_valid=%d\n",
					design->scratchpad_get_bool(PmuxProfileAuthorization::lifecycle_key, false));
			return;
		}
		if (mode == "empty_generation") {
			design->scratchpad_set_string(PmuxProfileAuthorization::generation_key, std::string());
			return;
		}
		if (mode == "lifecycle_false") {
			design->scratchpad_set_bool(PmuxProfileAuthorization::lifecycle_key, false);
			return;
		}
		log_cmd_error("unsupported snapshot reaudit mode %s.\n", mode.c_str());
	}
} snapshot_reaudit_pmux_wrapper;

} // namespace
