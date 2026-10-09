#ifndef PMUX_PROFILE_AUTHORIZATION_H
#define PMUX_PROFILE_AUTHORIZATION_H

#include "kernel/rtlil.h"

#include <algorithm>
#include <string>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxProfileAuthorization {

static constexpr const char *const enabled_key = "pmux_opt.accepted_constant_table";
static constexpr const char *const snapshot_key = "pmux_opt.accepted_constant_table_snapshot";

struct Snapshot
{
	std::string payload;
	int cell_count = 0;
};

inline void append_field(std::string &out, const std::string &tag, const std::string &value)
{
	out += tag;
	out += std::to_string(value.size());
	out += ':';
	out += value;
	out += ';';
}

inline std::string serialize_const(const RTLIL::Const &value)
{
	return std::to_string(value.flags) + ":" + std::to_string(value.size()) + ":" + value.as_string();
}

inline std::string serialize_signal(const RTLIL::SigSpec &signal)
{
	std::string result = std::to_string(GetSize(signal)) + ":";
	for (RTLIL::SigBit bit : signal) {
		if (bit.wire == nullptr) {
			result += "K" + std::to_string(int(bit.data)) + ";";
			continue;
		}
		result += "W";
		append_field(result, "N", bit.wire->name.str());
		result += "O" + std::to_string(bit.offset) + ";";
	}
	return result;
}

inline Snapshot build_constant_table_snapshot(RTLIL::Design *design)
{
	std::vector<RTLIL::Module *> modules = design->modules();
	std::sort(modules.begin(), modules.end(), [](const RTLIL::Module *lhs, const RTLIL::Module *rhs) {
		return lhs->name.str() < rhs->name.str();
	});

	Snapshot snapshot;
	snapshot.payload = "pmux-constant-table-snapshot-v1;";
	for (RTLIL::Module *module : modules) {
		std::vector<RTLIL::Cell *> cells;
		for (RTLIL::Cell *cell : module->cells())
			if (cell->type == ID(pmux_case34_lut4))
				cells.push_back(cell);
		std::sort(cells.begin(), cells.end(), [](const RTLIL::Cell *lhs, const RTLIL::Cell *rhs) {
			return lhs->name.str() < rhs->name.str();
		});

		for (RTLIL::Cell *cell : cells) {
			snapshot.cell_count++;
			append_field(snapshot.payload, "M", module->name.str());
			append_field(snapshot.payload, "C", cell->name.str());
			append_field(snapshot.payload, "T", cell->type.str());

			std::vector<RTLIL::IdString> parameter_names;
			for (const auto &item : cell->parameters)
				parameter_names.push_back(item.first);
			std::sort(parameter_names.begin(), parameter_names.end());
			snapshot.payload += "P" + std::to_string(parameter_names.size()) + ";";
			for (RTLIL::IdString name : parameter_names) {
				append_field(snapshot.payload, "N", name.str());
				append_field(snapshot.payload, "V", serialize_const(cell->parameters.at(name)));
			}

			std::vector<RTLIL::IdString> attribute_names;
			for (const auto &item : cell->attributes)
				attribute_names.push_back(item.first);
			std::sort(attribute_names.begin(), attribute_names.end());
			snapshot.payload += "R" + std::to_string(attribute_names.size()) + ";";
			for (RTLIL::IdString name : attribute_names) {
				append_field(snapshot.payload, "N", name.str());
				append_field(snapshot.payload, "V", serialize_const(cell->attributes.at(name)));
			}

			std::vector<RTLIL::IdString> port_names;
			for (const auto &item : cell->connections())
				port_names.push_back(item.first);
			std::sort(port_names.begin(), port_names.end());
			snapshot.payload += "Q" + std::to_string(port_names.size()) + ";";
			for (RTLIL::IdString name : port_names) {
				append_field(snapshot.payload, "N", name.str());
				append_field(snapshot.payload, "S", serialize_signal(cell->getPort(name)));
			}
		}
	}
	append_field(snapshot.payload, "COUNT", std::to_string(snapshot.cell_count));
	return snapshot;
}

inline void clear_constant_table_authorization(RTLIL::Design *design)
{
	design->scratchpad_set_bool(enabled_key, false);
	design->scratchpad_set_string(snapshot_key, std::string());
}

inline bool publish_constant_table_authorization(RTLIL::Design *design, bool accepted)
{
	clear_constant_table_authorization(design);
	if (!accepted)
		return false;
	Snapshot snapshot = build_constant_table_snapshot(design);
	if (snapshot.cell_count == 0)
		return false;
	design->scratchpad_set_string(snapshot_key, snapshot.payload);
	design->scratchpad_set_bool(enabled_key, true);
	return true;
}

inline bool constant_table_authorization_matches(RTLIL::Design *design)
{
	if (!design->scratchpad_get_bool(enabled_key, false))
		return false;
	const std::string expected = design->scratchpad_get_string(snapshot_key, std::string());
	if (expected.empty())
		return false;
	Snapshot actual = build_constant_table_snapshot(design);
	return actual.cell_count > 0 && actual.payload == expected;
}

} // namespace PmuxProfileAuthorization

YOSYS_NAMESPACE_END

#endif
