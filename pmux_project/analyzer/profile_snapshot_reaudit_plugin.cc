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
	const std::uintptr_t old_address = reinterpret_cast<std::uintptr_t>(old_cell);
	const auto parameters = old_cell->parameters;
	const auto attributes = old_cell->attributes;
	const auto connections = old_cell->connections();
	const auto before = PmuxProfileAuthorization::build_constant_table_snapshot(design);

	module->remove(old_cell);
	RTLIL::Cell *new_cell = nullptr;
	int attempts = 0;
	for (; attempts < 256; attempts++) {
		new_cell = module->addCell(name, type);
		new_cell->parameters = parameters;
		new_cell->attributes = attributes;
		for (const auto &connection : connections)
			new_cell->setPort(connection.first, connection.second);
		if (reinterpret_cast<std::uintptr_t>(new_cell) == old_address || attempts == 255)
			break;
		module->remove(new_cell);
	}

	const auto after = PmuxProfileAuthorization::build_constant_table_snapshot(design);
	log("A_PROFILE_SNAPSHOT_REBUILD module=%s cell=%s before_cells=%d after_cells=%d payload_equal=%d lifecycle_valid=%d pointer_reused=%d attempts=%d\n",
			log_id(module), log_id(new_cell), before.cell_count, after.cell_count,
			before.payload == after.payload,
			design->scratchpad_get_bool(PmuxProfileAuthorization::lifecycle_key, false),
			reinterpret_cast<std::uintptr_t>(new_cell) == old_address, attempts + 1);
}

void rebuild_same_name_module_and_contents(RTLIL::Design *design)
{
	RTLIL::Cell *target = first_target(design);
	if (target == nullptr)
		log_cmd_error("lifecycle reaudit found no target module.\n");
	RTLIL::Module *old_module = target->module;
	const auto before = PmuxProfileAuthorization::build_constant_table_snapshot(design);
	RTLIL::Module *new_module = old_module->clone();
	design->remove(old_module);
	design->add(new_module);
	const auto after = PmuxProfileAuthorization::build_constant_table_snapshot(design);
	log("A_PROFILE_MODULE_REBUILD module=%s before_cells=%d after_cells=%d payload_equal=%d lifecycle_valid=%d lifecycle_monitor_match=%d\n",
			log_id(new_module), before.cell_count, after.cell_count,
			before.payload == after.payload,
			design->scratchpad_get_bool(PmuxProfileAuthorization::lifecycle_key, false),
			PmuxProfileAuthorization::constant_table_lifecycle_matches(design));
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
		if (mode == "none") {
			log("A_PROFILE_LEGAL_GENERATION lifecycle_valid=%d lifecycle_monitor_match=%d\n",
					design->scratchpad_get_bool(PmuxProfileAuthorization::lifecycle_key, false),
					PmuxProfileAuthorization::constant_table_lifecycle_matches(design));
			return;
		}
		if (mode == "same_name_rebuild") {
			rebuild_same_name_and_contents(design);
			return;
		}
		if (mode == "module_rebuild") {
			rebuild_same_name_module_and_contents(design);
			return;
		}
		if (mode == "port_restore") {
			change_and_restore_port(design);
			return;
		}
		if (mode == "forge_generation") {
			design->scratchpad_set_string(PmuxProfileAuthorization::generation_key,
					"A-forged-generation");
			log("A_PROFILE_GENERATION_FORGED lifecycle_valid=%d lifecycle_monitor_match=%d\n",
					design->scratchpad_get_bool(PmuxProfileAuthorization::lifecycle_key, false),
					PmuxProfileAuthorization::constant_table_lifecycle_matches(design));
			return;
		}
		if (mode == "old_generation_replay") {
			const std::string old_generation = design->scratchpad_get_string(
					PmuxProfileAuthorization::generation_key, std::string());
			if (old_generation.empty())
				log_cmd_error("lifecycle reaudit found no published generation.\n");
			PmuxProfileAuthorization::publish_constant_table_authorization(design, true);
			const std::string new_generation = design->scratchpad_get_string(
					PmuxProfileAuthorization::generation_key, std::string());
			if (new_generation.empty() || new_generation == old_generation)
				log_cmd_error("lifecycle reaudit failed to create a distinct generation.\n");
			design->scratchpad_set_string(PmuxProfileAuthorization::generation_key,
					old_generation);
			log("A_PROFILE_OLD_GENERATION_REPLAY old=%s new=%s lifecycle_valid=%d lifecycle_monitor_match=%d\n",
					old_generation.c_str(), new_generation.c_str(),
					design->scratchpad_get_bool(PmuxProfileAuthorization::lifecycle_key, false),
					PmuxProfileAuthorization::constant_table_lifecycle_matches(design));
			return;
		}
		if (mode == "unregister_monitor") {
			const auto *monitor = PmuxProfileAuthorization::registered_lifecycle_monitor(design);
			if (monitor == nullptr)
				log_cmd_error("lifecycle reaudit found no registered monitor.\n");
			design->monitors.erase(const_cast<PmuxProfileAuthorization::LifecycleMonitor *>(monitor));
			log("A_PROFILE_MONITOR_UNREGISTERED lifecycle_valid=%d lifecycle_monitor_match=%d\n",
					design->scratchpad_get_bool(PmuxProfileAuthorization::lifecycle_key, false),
					PmuxProfileAuthorization::constant_table_lifecycle_matches(design));
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
