#include "pmux_v02_case12_proof_api.h"

#include "kernel/celltypes.h"
#include "kernel/sigtools.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {
namespace {

Case12CellSnapshot snapshot_cell(const RTLIL::Cell *cell)
{
	Case12CellSnapshot out;
	out.cell_id = cell->name.str(); out.type = cell->type;
	for (const auto &port : cell->connections()) out.ports.push_back({port.first, port.second});
	for (const auto &param : cell->parameters) out.parameters.push_back({param.first, param.second});
	for (const auto &attr : cell->attributes) out.attributes.push_back({attr.first, attr.second});
	std::sort(out.ports.begin(), out.ports.end(), [](const auto &a, const auto &b) { return a.port < b.port; });
	std::sort(out.parameters.begin(), out.parameters.end(), [](const auto &a, const auto &b) { return a.parameter < b.parameter; });
	std::sort(out.attributes.begin(), out.attributes.end(), [](const auto &a, const auto &b) { return a.attribute < b.attribute; });
	return out;
}

bool binary_const(const RTLIL::SigSpec &sig)
{
	for (RTLIL::SigBit bit : sig)
		if (bit.wire != nullptr || (bit.data != RTLIL::State::S0 && bit.data != RTLIL::State::S1)) return false;
	return true;
}

int unsigned_value(const RTLIL::SigSpec &sig)
{
	int value = 0;
	for (int i = 0; i < GetSize(sig); ++i)
		if (sig[i] == RTLIL::State::S1) value |= 1 << i;
	return value;
}

bool protected_cell(const RTLIL::Cell *cell)
{
	return cell->get_bool_attribute(ID::keep) || cell->get_bool_attribute(ID(dont_touch));
}

struct EqDecode { RTLIL::SigSpec state; int code = -1; };

bool decode_eq(RTLIL::Cell *cell, SigMap &sigmap, EqDecode &out)
{
	if (cell->type != ID($eq) || !cell->hasPort(ID::A) || !cell->hasPort(ID::B) ||
			!cell->hasPort(ID::Y) || GetSize(cell->getPort(ID::Y)) != 1)
		return false;
	if (cell->getParam(ID::A_SIGNED).as_bool() || cell->getParam(ID::B_SIGNED).as_bool()) return false;
	RTLIL::SigSpec a = cell->getPort(ID::A), b = cell->getPort(ID::B);
	if (binary_const(a) == binary_const(b)) return false;
	RTLIL::SigSpec state = binary_const(a) ? b : a;
	RTLIL::SigSpec constant = binary_const(a) ? a : b;
	if (GetSize(state) > 16 || GetSize(constant) > GetSize(state)) return false;
	out.state = sigmap(state);
	out.code = unsigned_value(constant);
	return true;
}

bool sig_equal(SigMap &sigmap, const RTLIL::SigSpec &a, const RTLIL::SigSpec &b)
{
	return sigmap(a) == sigmap(b);
}

class Analyzer final : public Case12Analyzer {
public:
	Case12ProofBundle analyze(RTLIL::Module *module) const override
	{
		Case12ProofBundle bundle;
		SigMap sigmap(module);
		NewCellTypes celltypes;
		celltypes.setup(module->design);
		const AnalysisBudget budget;
		std::size_t connected_bits = 0;
		for (RTLIL::Cell *cell : module->cells())
			for (const auto &conn : cell->connections())
				connected_bits += GetSize(conn.second);
		if (module->cells().size() > budget.max_cells) {
			bundle.reason_counts["budget_cells_exhausted"]++;
			return bundle;
		}
		if (connected_bits > budget.max_bits) {
			bundle.reason_counts["budget_bits_exhausted"]++;
			return bundle;
		}
		std::map<RTLIL::SigBit, int> driver_count;
		for (RTLIL::Cell *cell : module->cells())
			if (celltypes.cell_known(cell->type))
				for (const auto &conn : cell->connections())
					if (celltypes.cell_output(cell->type, conn.first))
						for (RTLIL::SigBit bit : conn.second)
							driver_count[sigmap(bit)]++;
		std::map<RTLIL::SigBit, std::pair<RTLIL::Cell *, EqDecode>> eq_by_y;
		std::set<RTLIL::SigBit> ambiguous_eq_outputs;
		for (RTLIL::Cell *cell : module->cells()) {
			EqDecode decode;
			if (!decode_eq(cell, sigmap, decode)) continue;
			RTLIL::SigBit y = sigmap(cell->getPort(ID::Y)[0]);
			if (ambiguous_eq_outputs.count(y)) continue;
			if (!eq_by_y.count(y)) {
				eq_by_y[y] = {cell, decode};
			} else {
				eq_by_y.erase(y);
				ambiguous_eq_outputs.insert(y);
			}
		}

		std::vector<RTLIL::Cell *> pmuxes;
		for (RTLIL::Cell *cell : module->cells()) if (cell->type == ID($pmux)) pmuxes.push_back(cell);
		const std::size_t pair_events = pmuxes.size() < 2 ? 0 : pmuxes.size() * (pmuxes.size() - 1) / 2;
		const std::size_t event_estimate = connected_bits + 5 * module->cells().size() + pair_events;
		if (event_estimate > budget.max_events) {
			bundle.reason_counts["budget_events_exhausted"]++;
			return bundle;
		}
		std::sort(pmuxes.begin(), pmuxes.end(), [](auto *a, auto *b) { return a->name < b->name; });
		for (int i = 0; i < GetSize(pmuxes); ++i) for (int j = i + 1; j < GetSize(pmuxes); ++j) {
			RTLIL::Cell *first = pmuxes[i], *second = pmuxes[j];
			PairedPmuxProof proof; proof.module = module->name.str();
			proof.first = snapshot_cell(first); proof.second = snapshot_cell(second);
			if (protected_cell(first) || protected_cell(second)) { proof.reason = "protected_cell"; continue; }
			int width = first->getParam(ID::WIDTH).as_int();
			int swidth = first->getParam(ID::S_WIDTH).as_int();
			if (width <= 0 || swidth <= 0 || swidth > 32767 ||
					second->getParam(ID::WIDTH).as_int() != width ||
					second->getParam(ID::S_WIDTH).as_int() != swidth ||
					GetSize(first->getPort(ID::A)) != width || GetSize(second->getPort(ID::A)) != width ||
					GetSize(first->getPort(ID::Y)) != width || GetSize(second->getPort(ID::Y)) != width ||
					GetSize(first->getPort(ID::S)) != swidth || GetSize(second->getPort(ID::S)) != swidth ||
					GetSize(first->getPort(ID::B)) != width * swidth ||
					GetSize(second->getPort(ID::B)) != width * swidth ||
					!sig_equal(sigmap, first->getPort(ID::S), second->getPort(ID::S))) continue;
			bool unique_outputs = true;
			for (RTLIL::SigBit bit : first->getPort(ID::Y)) unique_outputs &= driver_count[sigmap(bit)] == 1;
			for (RTLIL::SigBit bit : second->getPort(ID::Y)) unique_outputs &= driver_count[sigmap(bit)] == 1;
			if (!unique_outputs) continue;
			int states = swidth + 1, m = 0;
			while ((1 << m) < states) ++m;
			if ((1 << m) != states || m < 2) continue;
			std::vector<int> index_by_code(states, -1);
			RTLIL::SigSpec common_state;
			bool decode_ok = true;
			for (int s = 0; s < swidth; ++s) {
				auto found = eq_by_y.find(sigmap(first->getPort(ID::S)[s]));
				if (found == eq_by_y.end()) { decode_ok = false; break; }
				const EqDecode &decode = found->second.second;
				if (GetSize(decode.state) != m || decode.code <= 0 || decode.code >= states ||
						index_by_code[decode.code] != -1 ||
						(!common_state.empty() && decode.state != common_state)) {
					decode_ok = false; break;
				}
				common_state = decode.state; index_by_code[decode.code] = s;
			}
			for (int code = 1; code < states; ++code) if (index_by_code[code] < 0) decode_ok = false;
			if (!decode_ok) continue;
			proof.first_data_by_code.resize(states); proof.second_data_by_code.resize(states);
			for (int code = 1; code < states; ++code) {
				proof.first_data_by_code[code] = first->getPort(ID::B).extract(index_by_code[code] * width, width);
				proof.second_data_by_code[code] = second->getPort(ID::B).extract(index_by_code[code] * width, width);
			}
			bool cross = true;
			for (int n = 1; n < states / 2; ++n) {
				cross &= sig_equal(sigmap, proof.first_data_by_code[2*n], proof.second_data_by_code[2*n+1]);
				cross &= sig_equal(sigmap, proof.second_data_by_code[2*n], proof.first_data_by_code[2*n+1]);
			}
			if (!cross) continue;
			proof.width = width; proof.state_width = m; proof.state_lsb_first = common_state;
			proof.select = first->getPort(ID::S); proof.first_a = first->getPort(ID::A);
			proof.second_a = second->getPort(ID::A); proof.first_y = first->getPort(ID::Y);
			proof.second_y = second->getPort(ID::Y); proof.complete_decode = true;
			proof.cross_relation_proved = true; proof.proven_all_obligations = true; proof.reason = "proved";
			bundle.paired_pmux.push_back(std::move(proof));
		}

		std::map<RTLIL::SigBit, std::vector<std::pair<RTLIL::Cell *, RTLIL::IdString>>> users;
		std::map<RTLIL::SigBit, RTLIL::Cell *> mux_driver;
		std::set<RTLIL::SigBit> ambiguous_mux_drivers;
		std::set<RTLIL::SigBit> module_output_bits;
		for (RTLIL::Wire *wire : module->wires())
			if (wire->port_output)
				for (RTLIL::SigBit bit : RTLIL::SigSpec(wire))
					module_output_bits.insert(sigmap(bit));
		for (RTLIL::Cell *cell : module->cells()) {
			for (const auto &conn : cell->connections()) {
				bool input = celltypes.cell_known(cell->type) && celltypes.cell_input(cell->type, conn.first);
				bool output = celltypes.cell_known(cell->type) && celltypes.cell_output(cell->type, conn.first);
				for (RTLIL::SigBit bit : conn.second) {
					if (input) users[sigmap(bit)].push_back({cell, conn.first});
					if (output && cell->type == ID($mux)) {
						RTLIL::SigBit canonical = sigmap(bit);
						if (mux_driver.count(canonical) && mux_driver.at(canonical) != cell) {
							mux_driver.erase(canonical);
							ambiguous_mux_drivers.insert(canonical);
						} else if (!ambiguous_mux_drivers.count(canonical)) {
							mux_driver[canonical] = cell;
						}
					}
				}
			}
		}
		std::set<std::string> emitted;
		for (RTLIL::Cell *outer : module->cells()) {
			if (outer->type != ID($mux) || protected_cell(outer)) continue;
			PriorityMuxProof proof; proof.module = module->name.str(); proof.width = outer->getParam(ID::WIDTH).as_int();
			RTLIL::Cell *cell = outer; bool ok = proof.width > 0;
			for (RTLIL::SigBit bit : outer->getPort(ID::Y)) ok &= driver_count[sigmap(bit)] == 1;
			for (int depth = 0; depth < 4 && ok; ++depth) {
				if (cell == nullptr || cell->type != ID($mux) || protected_cell(cell) ||
						cell->getParam(ID::WIDTH).as_int() != proof.width || GetSize(cell->getPort(ID::S)) != 1 ||
						GetSize(cell->getPort(ID::A)) != proof.width || GetSize(cell->getPort(ID::B)) != proof.width ||
						GetSize(cell->getPort(ID::Y)) != proof.width) { ok = false; break; }
				proof.cells_outer_to_inner.push_back(snapshot_cell(cell));
				proof.select_outer_to_inner.push_back(cell->getPort(ID::S)[0]);
				proof.data_outer_to_inner.push_back(cell->getPort(ID::B));
				if (depth == 0) proof.output = cell->getPort(ID::Y);
				RTLIL::SigSpec next = cell->getPort(ID::A);
				if (depth == 3) { proof.zero_default = binary_const(next) && unsigned_value(next) == 0; break; }
				RTLIL::Cell *next_cell = nullptr;
				for (RTLIL::SigBit bit : next) {
					auto found = mux_driver.find(sigmap(bit));
					if (found == mux_driver.end() || driver_count[sigmap(bit)] != 1 ||
							(next_cell != nullptr && next_cell != found->second)) { ok = false; break; }
					next_cell = found->second;
				}
				if (!ok || next_cell == nullptr || !sig_equal(sigmap, next_cell->getPort(ID::Y), next)) { ok = false; break; }
				for (RTLIL::SigBit bit : next)
					if (users[sigmap(bit)].size() != 1 || module_output_bits.count(sigmap(bit))) ok = false;
				cell = next_cell;
			}
			if (!ok || !proof.zero_default || proof.cells_outer_to_inner.size() != 4) continue;
			bool unique = true;
			for (const auto &snap : proof.cells_outer_to_inner) unique &= !emitted.count(snap.cell_id);
			if (!unique) continue;
			for (const auto &snap : proof.cells_outer_to_inner) emitted.insert(snap.cell_id);
			proof.exclusive_intermediate_fanout = true; proof.proven_all_obligations = true; proof.reason = "proved";
			bundle.priority_mux4.push_back(std::move(proof));
		}
		return bundle;
	}
};

} // namespace

std::unique_ptr<Case12Analyzer> make_case12_analyzer() { return std::unique_ptr<Case12Analyzer>(new Analyzer); }

} // namespace PmuxOpt

YOSYS_NAMESPACE_END
