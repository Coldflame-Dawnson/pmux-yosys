#include "case12_rewrite.h"

#include "pmux_v02_case12_proof_api.h"

#include <algorithm>
#include <set>

YOSYS_NAMESPACE_BEGIN
namespace PmuxOpt {
namespace {

bool same_snapshot(RTLIL::Module *module, const Case12CellSnapshot &snapshot)
{
	RTLIL::Cell *cell = module->cell(RTLIL::IdString(snapshot.cell_id));
	if (cell == nullptr || cell->type != snapshot.type ||
			cell->connections().size() != snapshot.ports.size() ||
			cell->parameters.size() != snapshot.parameters.size() ||
			cell->attributes.size() != snapshot.attributes.size()) return false;
	for (const auto &port : snapshot.ports)
		if (!cell->hasPort(port.port) || cell->getPort(port.port) != port.signal) return false;
	for (const auto &param : snapshot.parameters)
		if (!cell->hasParam(param.parameter) || cell->getParam(param.parameter) != param.value) return false;
	for (const auto &attr : snapshot.attributes) {
		auto found = cell->attributes.find(attr.attribute);
		if (found == cell->attributes.end() || found->second != attr.value) return false;
	}
	return true;
}

RTLIL::SigSpec new_wire(RTLIL::Module *module, int width)
{
	return RTLIL::SigSpec(module->addWire(NEW_ID, width));
}

int apply_paired(RTLIL::Module *module, const PairedPmuxProof &proof)
{
	const int leaves_count = 1 << (proof.state_width - 1);
	std::vector<RTLIL::SigSpec> x(leaves_count), y(leaves_count);
	x[0] = new_wire(module, proof.width); y[0] = new_wire(module, proof.width);
	module->addMux(NEW_ID, proof.first_a, proof.second_data_by_code.at(1),
			proof.state_lsb_first[0], x[0]);
	module->addMux(NEW_ID, proof.second_a, proof.first_data_by_code.at(1),
			proof.state_lsb_first[0], y[0]);
	for (int n = 1; n < leaves_count; ++n) {
		x[n] = proof.first_data_by_code.at(2*n);
		y[n] = proof.second_data_by_code.at(2*n);
	}
	int added = 2;
	for (int bit = 1; bit < proof.state_width; ++bit) {
		std::vector<RTLIL::SigSpec> nx, ny;
		for (int i = 0; i < GetSize(x); i += 2) {
			RTLIL::SigSpec wx = new_wire(module, proof.width), wy = new_wire(module, proof.width);
			module->addMux(NEW_ID, x[i], x[i+1], proof.state_lsb_first[bit], wx);
			module->addMux(NEW_ID, y[i], y[i+1], proof.state_lsb_first[bit], wy);
			nx.push_back(wx); ny.push_back(wy); added += 2;
		}
		x.swap(nx); y.swap(ny);
	}
	module->addMux(NEW_ID, x[0], y[0], proof.state_lsb_first[0], proof.first_y);
	module->addMux(NEW_ID, y[0], x[0], proof.state_lsb_first[0], proof.second_y);
	added += 2;
	module->remove(module->cell(RTLIL::IdString(proof.first.cell_id)));
	module->remove(module->cell(RTLIL::IdString(proof.second.cell_id)));
	return added;
}

RTLIL::Cell *add_lcell(RTLIL::Module *module, uint16_t mask,
		RTLIL::SigBit a, RTLIL::SigBit b, RTLIL::SigBit c, RTLIL::SigBit d,
		RTLIL::SigBit output)
{
	RTLIL::Cell *cell = module->addCell(NEW_ID, ID(cycloneiv_lcell_comb));
	cell->setParam(ID(lut_mask), RTLIL::Const(mask, 16));
	cell->setPort(ID(dataa), a); cell->setPort(ID(datab), b);
	cell->setPort(ID(datac), c); cell->setPort(ID(datad), d);
	cell->setPort(ID(cin), RTLIL::State::S0); cell->setPort(ID(combout), output);
	return cell;
}

int apply_priority(RTLIL::Module *module, const PriorityMuxProof &proof)
{
	for (int bit = 0; bit < proof.width; ++bit) {
		RTLIL::Wire *w0 = module->addWire(NEW_ID), *w1 = module->addWire(NEW_ID);
		add_lcell(module, 0xe4a0,
				proof.select_outer_to_inner[2], proof.select_outer_to_inner[3],
				proof.data_outer_to_inner[2][bit], proof.data_outer_to_inner[3][bit], RTLIL::SigBit(w0));
		add_lcell(module, 0x00d8,
				proof.select_outer_to_inner[1], proof.data_outer_to_inner[1][bit], RTLIL::SigBit(w0),
				RTLIL::State::S0, RTLIL::SigBit(w1));
		add_lcell(module, 0x00d8,
				proof.select_outer_to_inner[0], proof.data_outer_to_inner[0][bit], RTLIL::SigBit(w1),
				RTLIL::State::S0, proof.output[bit]);
	}
	for (const Case12CellSnapshot &snapshot : proof.cells_outer_to_inner)
		module->remove(module->cell(RTLIL::IdString(snapshot.cell_id)));
	return proof.width * 3;
}

bool malformed_eq_width_metadata(const RTLIL::Cell *cell)
{
	if (cell->type != ID($eq)) return false;
	if (!cell->hasPort(ID::A) || !cell->hasPort(ID::B) || !cell->hasPort(ID::Y) ||
			!cell->hasParam(ID::A_WIDTH) || !cell->hasParam(ID::B_WIDTH) ||
			!cell->hasParam(ID::Y_WIDTH))
		return true;
	return cell->getParam(ID::A_WIDTH).as_int() != GetSize(cell->getPort(ID::A)) ||
			cell->getParam(ID::B_WIDTH).as_int() != GetSize(cell->getPort(ID::B)) ||
			cell->getParam(ID::Y_WIDTH).as_int() != GetSize(cell->getPort(ID::Y));
}

} // namespace

Case12RewriteResult run_case12_structural_rewrite(const std::vector<RTLIL::Module *> &modules)
{
	Case12RewriteResult result;
	// Validate the complete selected input before producing any plan.  Besides
	// being outside the proof contract, malformed comparison metadata can make
	// downstream Yosys analyses dereference inconsistent port widths.
	for (RTLIL::Module *module : modules)
		for (RTLIL::Cell *cell : module->cells())
			if (malformed_eq_width_metadata(cell)) {
				result.malformed_cell_metadata = true;
				result.reason_counts["eq_port_width_mismatch"]++;
			}
	if (result.malformed_cell_metadata)
		return result;
	for (RTLIL::Module *module : modules) {
		Case12ProofBundle bundle = make_case12_analyzer()->analyze(module);
		for (const auto &item : bundle.reason_counts)
			result.reason_counts[item.first] += item.second;
		result.paired_candidates += GetSize(bundle.paired_pmux);
		result.priority_candidates += GetSize(bundle.priority_mux4);
		std::set<std::string> occupied;
		for (const PairedPmuxProof &proof : bundle.paired_pmux) {
			if (!proof.proven_all_obligations || !proof.complete_decode || !proof.cross_relation_proved) {
				result.reason_counts["paired_proof_incomplete"]++; continue;
			}
			if (occupied.count(proof.first.cell_id) || occupied.count(proof.second.cell_id) ||
					!same_snapshot(module, proof.first) || !same_snapshot(module, proof.second)) {
				result.reason_counts["paired_snapshot_conflict"]++; continue;
			}
			occupied.insert(proof.first.cell_id); occupied.insert(proof.second.cell_id);
			result.added_cells += apply_paired(module, proof); result.removed_cells += 2;
			result.committed_candidates++;
		}
		for (const PriorityMuxProof &proof : bundle.priority_mux4) {
			bool snapshots = proof.proven_all_obligations && proof.zero_default && proof.exclusive_intermediate_fanout;
			for (const Case12CellSnapshot &snapshot : proof.cells_outer_to_inner)
				snapshots &= !occupied.count(snapshot.cell_id) && same_snapshot(module, snapshot);
			if (!snapshots) { result.reason_counts["priority_snapshot_conflict"]++; continue; }
			for (const Case12CellSnapshot &snapshot : proof.cells_outer_to_inner) occupied.insert(snapshot.cell_id);
			result.added_cells += apply_priority(module, proof); result.removed_cells += 4;
			result.committed_candidates++;
		}
	}
	return result;
}

} // namespace PmuxOpt
YOSYS_NAMESPACE_END
