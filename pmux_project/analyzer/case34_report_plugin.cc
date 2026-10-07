#include "pmux_v02_case34_proof_api.h"

#include "backends/rtlil/rtlil_backend.h"
#include "kernel/celltypes.h"
#include "kernel/log.h"
#include "kernel/register.h"
#include "kernel/sigtools.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {
namespace {

struct RBitLess {
	bool operator()(const RTLIL::SigBit &a, const RTLIL::SigBit &b) const
	{
		if ((a.wire == nullptr) != (b.wire == nullptr)) return a.wire == nullptr;
		if (!a.wire) return int(a.data) < int(b.data);
		if (a.wire->name != b.wire->name) return a.wire->name.str() < b.wire->name.str();
		return a.offset < b.offset;
	}
};

struct RIndex {
	RTLIL::Module *module;
	SigMap sigmap;
	NewCellTypes celltypes;
	std::map<RTLIL::SigBit, std::vector<std::pair<RTLIL::Cell *, int>>, RBitLess> drivers;
	std::map<RTLIL::SigBit, std::vector<FanoutUseSnapshot>, RBitLess> uses;

	explicit RIndex(RTLIL::Module *m) : module(m), sigmap(m)
	{
		celltypes.setup(m->design);
		for (RTLIL::Cell *cell : m->cells())
			for (const auto &conn : cell->connections()) {
				bool out = celltypes.cell_known(cell->type) && celltypes.cell_output(cell->type, conn.first);
				bool in = celltypes.cell_known(cell->type) && celltypes.cell_input(cell->type, conn.first);
				for (int i = 0; i < GetSize(conn.second); ++i) {
					RTLIL::SigBit bit = sigmap(conn.second[i]); if (!bit.wire) continue;
					if (out) drivers[bit].push_back({cell, i});
					if (in) uses[bit].push_back({cell->name.str(), conn.first, i, bit});
				}
			}
		for (auto &item : uses)
			std::sort(item.second.begin(), item.second.end(), [](const FanoutUseSnapshot &a, const FanoutUseSnapshot &b) {
				return std::tie(a.cell_id, a.port, a.port_bit) < std::tie(b.cell_id, b.port, b.port_bit);
			});
	}

	RTLIL::Cell *full_driver(const RTLIL::SigSpec &raw) const
	{
		RTLIL::SigSpec sig = sigmap(raw); RTLIL::Cell *cell = nullptr;
		for (int i = 0; i < GetSize(sig); ++i) {
			auto it = drivers.find(sig[i]); if (it == drivers.end() || it->second.size() != 1) return nullptr;
			if (!cell) cell = it->second.front().first;
			if (cell != it->second.front().first || it->second.front().second != i) return nullptr;
		}
		return cell;
	}

	CellSnapshot snapshot(RTLIL::Cell *cell) const
	{
		CellSnapshot out; out.cell_id = cell->name.str(); out.type = cell->type;
		for (const auto &conn : cell->connections()) out.ports.push_back({conn.first, conn.second});
		for (const auto &param : cell->parameters) out.parameters.push_back({param.first, param.second});
		return out;
	}
};

std::string fingerprint(RTLIL::Design *design)
{
	std::ostringstream out; std::vector<RTLIL::Module *> modules = design->modules();
	std::sort(modules.begin(), modules.end(), [](RTLIL::Module *a, RTLIL::Module *b) { return a->name.str() < b->name.str(); });
	for (RTLIL::Module *module : modules) RTLIL_BACKEND::dump_module(out, "", module, design, false);
	return out.str();
}

bool compare_source(RIndex &index, RTLIL::Cell *cell, RTLIL::SigSpec &source, RTLIL::Const &constant)
{
	if (!cell || !cell->type.in(ID($eq), ID($ne))) return false;
	RTLIL::SigSpec a = index.sigmap(cell->getPort(ID::A)), b = index.sigmap(cell->getPort(ID::B));
	auto fully_const = [](const RTLIL::SigSpec &sig) {
		for (RTLIL::SigBit bit : sig) if (bit.wire || (bit.data != RTLIL::State::S0 && bit.data != RTLIL::State::S1)) return false;
		return true;
	};
	if (fully_const(a) == fully_const(b)) return false;
	RTLIL::SigSpec c = fully_const(a) ? a : b; source = fully_const(a) ? b : a; constant = c.as_const(); return true;
}

void report_case3(RTLIL::Module *module)
{
	RIndex index(module); auto analyzer = make_guarded_cone_analyzer(); int reports = 0;
	std::vector<RTLIL::Cell *> cells = module->cells();
	std::sort(cells.begin(), cells.end(), [](RTLIL::Cell *a, RTLIL::Cell *b) { return a->name.str() < b->name.str(); });
	for (RTLIL::Cell *outer : cells) {
		if (outer->type != ID($mux) || GetSize(outer->getPort(ID::S)) != 1) continue;
		RTLIL::Cell *inner = index.full_driver(outer->getPort(ID::B));
		if (!inner || inner->type != ID($mux) || GetSize(inner->getPort(ID::S)) != 1) continue;
		RTLIL::Cell *pmux = index.full_driver(inner->getPort(ID::B));
		if (!pmux || pmux->type != ID($pmux)) continue;
		int width = pmux->getParam(ID::WIDTH).as_int(), branches = pmux->getParam(ID::S_WIDTH).as_int();
		RTLIL::SigSpec select = pmux->getPort(ID::S), source;
		std::vector<RTLIL::Cell *> comparators; std::vector<RTLIL::Const> constants;
		bool valid = true;
		for (int i = 0; i < branches; ++i) {
			RTLIL::Cell *cmp = index.full_driver(RTLIL::SigSpec(select[i])); RTLIL::SigSpec local_source; RTLIL::Const constant;
			if (!compare_source(index, cmp, local_source, constant)) { valid = false; break; }
			if (i == 0) source = local_source; else if (source != local_source) { valid = false; break; }
			comparators.push_back(cmp); constants.push_back(constant);
		}
		if (!valid || source.empty()) continue;
		RTLIL::SigBit inner_select = index.sigmap(inner->getPort(ID::S)[0]); int removed = -1;
		for (int i = 0; i < GetSize(source); ++i) if (index.sigmap(source[i]) == inner_select) removed = i;
		if (removed < 0) continue;

		GuardedConeProofRequest request;
		request.module_id = module->name.str(); request.module = module;
		request.guard_sink_cell_id = outer->name.str(); request.guard_sink_cell = outer;
		request.guard_facts = {{outer->getPort(ID::S)[0], RTLIL::State::S1}, {inner->getPort(ID::S)[0], RTLIL::State::S1}};
		request.source_value = source; request.source_width = GetSize(source); request.removed_bit_indices = {removed};
		request.affected_cells = {outer->name.str(), inner->name.str(), pmux->name.str()};
		for (int i = 0; i < branches; ++i) {
			ComparatorNarrowingRequest cmp;
			cmp.cell_id = comparators[i]->name.str(); cmp.cell = comparators[i]; cmp.source_snapshot = source;
			cmp.constant_snapshot = constants[i]; cmp.removed_bit_indices = {removed};
			RTLIL::SigBit y = index.sigmap(comparators[i]->getPort(ID::Y)[0]); cmp.full_fanout_snapshot = index.uses[y];
			request.comparators.push_back(cmp); request.affected_cells.push_back(comparators[i]->name.str());
			for (const FanoutUseSnapshot &use : cmp.full_fanout_snapshot) request.affected_cells.push_back(use.cell_id);
		}
		RTLIL::SigSpec data = pmux->getPort(ID::B);
		for (int branch = 0; branch < branches; ++branch)
			for (int bit = 0; bit < width; ++bit) {
				RTLIL::SigBit value = index.sigmap(data[branch * width + bit]);
				for (int source_bit = 0; source_bit < GetSize(source); ++source_bit)
					if (value == index.sigmap(source[source_bit]))
						request.folded_data_bits.push_back({branch * width + bit, data[branch * width + bit],
								constants[branch][source_bit], branch});
			}
		for (int bit = 0; bit < width; ++bit)
			if (pmux->getPort(ID::A)[bit].wire)
				request.preserved_variable_bits.push_back({branches * width + bit, pmux->getPort(ID::A)[bit]});
		if (request.folded_data_bits.empty() || request.preserved_variable_bits.empty()) continue;
		GuardedConeProof proof = analyzer->prove(request);
		log("CASE34_GUARD_PROOF module=%s sink=%s source_width=%d removed=%d comparators=%zu folded=%zu preserved=%zu status=%d all=%d reason=%s cells=%zu bits=%zu events=%zu\n",
				log_id(module), log_id(outer), request.source_width, removed, proof.comparator_cells.size(),
				proof.folded_data_bits.size(), proof.preserved_variable_bits.size(), int(proof.status),
				proof.proven_all_obligations, v02_reason_code_name(proof.reason_code), proof.usage.visited_cells,
				proof.usage.visited_bits, proof.usage.propagation_events);
		for (const ComparatorProofConclusion &cmp : proof.comparator_cells)
			log("CASE34_GUARD_COMPARATOR cell=%s full_width=%d reduced_width=%d equivalent=%d preserve=%d reason=%s\n",
					cmp.cell_id.c_str(), cmp.full_width, cmp.reduced_width, cmp.equivalent_under_guard,
					cmp.comparator_must_be_preserved, v02_reason_code_name(cmp.reason_code));
		++reports;
	}
	log("CASE34_GUARD_REPORTS %d\n", reports);
}

struct DiscoveredTable {
	bool ok = false;
	std::vector<RTLIL::SigBit> order;
	std::set<std::string> cells;
};

DiscoveredTable discover_table(RIndex &index, const RTLIL::SigSpec &raw)
{
	RTLIL::SigSpec sig = index.sigmap(raw); bool constant = true;
	for (RTLIL::SigBit bit : sig) constant &= !bit.wire;
	if (constant) return {sig.is_fully_def(), {}, {}};
	RTLIL::Cell *cell = index.full_driver(sig);
	if (!cell || cell->type != ID($mux) || GetSize(cell->getPort(ID::S)) != 1) return {};
	DiscoveredTable a = discover_table(index, cell->getPort(ID::A));
	DiscoveredTable b = discover_table(index, cell->getPort(ID::B));
	if (!a.ok || !b.ok || a.order.size() != b.order.size()) return {};
	for (std::size_t i = 0; i < a.order.size(); ++i) if (index.sigmap(a.order[i]) != index.sigmap(b.order[i])) return {};
	DiscoveredTable out; out.ok = true; out.order = a.order; out.order.push_back(index.sigmap(cell->getPort(ID::S)[0]));
	out.cells = a.cells; out.cells.insert(b.cells.begin(), b.cells.end()); out.cells.insert(cell->name.str()); return out;
}

bool is_seq(RTLIL::IdString type)
{
	std::string s = type.str(); return s.find("dff") != std::string::npos || s.find("DFF") != std::string::npos;
}

bool reaches(RIndex &index, RTLIL::SigBit root, RTLIL::Cell *sink)
{
	std::deque<RTLIL::SigBit> queue; std::set<RTLIL::SigBit, RBitLess> seen; queue.push_back(index.sigmap(root));
	while (!queue.empty()) {
		RTLIL::SigBit bit = queue.front(); queue.pop_front(); if (!seen.insert(bit).second) continue;
		for (const FanoutUseSnapshot &use : index.uses[bit]) {
			RTLIL::Cell *cell = index.module->cell(RTLIL::IdString(use.cell_id));
			if (cell == sink && use.port == ID::D) return true;
			if (!cell || is_seq(cell->type)) continue;
			if (cell->type == ID($mux) && use.port.in(ID::A, ID::B) &&
					use.port_bit >= 0 && use.port_bit < GetSize(cell->getPort(ID::Y))) {
				queue.push_back(index.sigmap(cell->getPort(ID::Y)[use.port_bit]));
				continue;
			}
			for (const auto &conn : cell->connections())
				if (index.celltypes.cell_output(cell->type, conn.first))
					for (RTLIL::SigBit output : conn.second) queue.push_back(index.sigmap(output));
		}
	}
	return false;
}

void report_case4(RTLIL::Module *module)
{
	RIndex index(module); RTLIL::SigSpec best_root; DiscoveredTable best;
	for (RTLIL::Cell *cell : module->cells()) {
		if (cell->type != ID($mux)) continue;
		DiscoveredTable found = discover_table(index, cell->getPort(ID::Y));
		if (found.ok && found.order.size() > best.order.size()) { best = found; best_root = cell->getPort(ID::Y); }
	}
	if (!best.ok) { log("CASE34_TABLE_REPORTS 0\n"); return; }
	ConstantTableProofRequest request;
	request.module_id = module->name.str(); request.module = module; request.root_outputs = best_root;
	request.address_bits_lsb_first = best.order;
	for (RTLIL::SigBit bit : best_root) request.output_bits_lsb_first.push_back(bit);
	request.source_cells.assign(best.cells.begin(), best.cells.end());
	for (RTLIL::Cell *cell : module->cells()) if (is_seq(cell->type)) {
		SequentialSinkRequest sink; sink.cell_id = cell->name.str(); sink.cell = cell; sink.snapshot = index.snapshot(cell);
		for (int bit = 0; bit < GetSize(best_root); ++bit) if (reaches(index, best_root[bit], cell)) sink.root_output_bit_indices.push_back(bit);
		if (!sink.root_output_bit_indices.empty()) request.sequential_sinks.push_back(sink);
	}
	ConstantTableProof proof = make_constant_table_analyzer()->prove(request);
	log("CASE34_TABLE_PROOF module=%s address_width=%d output_width=%d rows=%zu source_cells=%zu sinks=%zu status=%d complete=%d all=%d reason=%s cells=%zu bits=%zu events=%zu\n",
			log_id(module), proof.address_width, proof.output_width, proof.truth_table.size(), proof.source_cells.size(),
			proof.sequential_sinks.size(), int(proof.status), proof.complete_binary_domain, proof.proven_all_rows,
			v02_reason_code_name(proof.reason_code), proof.usage.visited_cells, proof.usage.visited_bits,
			proof.usage.propagation_events);
	for (std::size_t row = 0; row < proof.truth_table.size(); ++row)
		log("CASE34_TABLE_ROW address=%zu bits=%s\n", row, proof.truth_table[row].as_string().c_str());
	log("CASE34_TABLE_REPORTS 1\n");
}

struct Case34ProofReportPass : public Pass {
	Case34ProofReportPass() : Pass("pmux_case34_proof_report", "emit read-only v0.2 case3/case4 proof diagnostics") { }
	void help() override { log("    pmux_case34_proof_report\n\nDiscover and prove guarded cones and complete constant tables without modifying RTLIL.\n"); }
	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		extra_args(args, 1, design); const std::string before = fingerprint(design);
		for (RTLIL::Module *module : design->selected_modules()) { report_case3(module); report_case4(module); }
		log_assert(before == fingerprint(design)); log("CASE34_PROOF_NETLIST_UNCHANGED 1\n");
	}
} Case34ProofReportPass;

} // namespace
} // namespace PmuxOpt

YOSYS_NAMESPACE_END
