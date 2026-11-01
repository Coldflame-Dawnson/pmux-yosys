#include "pmux_v02_case34_proof_api.h"
#include "budget_tracker.h"
#include "guarded_fact_analysis.h"

#include "kernel/celltypes.h"
#include "kernel/sigtools.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {
namespace {

using PmuxOptTestable::BudgetKind;
using PmuxOptTestable::BudgetTracker;

struct BitLess {
	bool operator()(const RTLIL::SigBit &a, const RTLIL::SigBit &b) const
	{
		if ((a.wire == nullptr) != (b.wire == nullptr)) return a.wire == nullptr;
		if (a.wire == nullptr) return int(a.data) < int(b.data);
		if (a.wire->name != b.wire->name) return a.wire->name.str() < b.wire->name.str();
		return a.offset < b.offset;
	}
};

struct Driver {
	RTLIL::Cell *cell = nullptr;
	RTLIL::IdString port;
	int offset = -1;
};

bool exact_budget(const AnalysisBudget &b)
{
	return b.max_cells == 512 && b.max_bits == 8192 && b.max_events == 262144;
}

bool binary(RTLIL::State s)
{
	return s == RTLIL::State::S0 || s == RTLIL::State::S1;
}

V02ReasonCode budget_code(BudgetKind kind)
{
	if (kind == BudgetKind::Cells) return V02ReasonCode::BudgetCellsExhausted;
	if (kind == BudgetKind::Bits) return V02ReasonCode::BudgetBitsExhausted;
	return V02ReasonCode::BudgetEventsExhausted;
}

V02ReasonCode entry_reason(const EntryAnalysisResult &r)
{
	if (r.status == EntryStatus::FactConflict) return V02ReasonCode::FactConflict;
	if (r.reason_code == "budget_cells_exhausted") return V02ReasonCode::BudgetCellsExhausted;
	if (r.reason_code == "budget_bits_exhausted") return V02ReasonCode::BudgetBitsExhausted;
	if (r.reason_code == "budget_events_exhausted") return V02ReasonCode::BudgetEventsExhausted;
	return V02ReasonCode::DataBitUnknown;
}

V02ProofStatus entry_status(const EntryAnalysisResult &r)
{
	if (r.status == EntryStatus::FactConflict) return V02ProofStatus::FactConflict;
	if (r.status == EntryStatus::BudgetExhausted) return V02ProofStatus::BudgetExhausted;
	if (r.status == EntryStatus::InvalidRequest) return V02ProofStatus::InvalidRequest;
	return V02ProofStatus::Unknown;
}

bool add_usage(ResourceUsage &dst, const ResourceUsage &src, const AnalysisBudget &limit,
		V02ReasonCode &reason)
{
	dst.visited_cells += src.visited_cells;
	dst.visited_bits += src.visited_bits;
	dst.propagation_events += src.propagation_events;
	if (dst.visited_cells > limit.max_cells) reason = V02ReasonCode::BudgetCellsExhausted;
	else if (dst.visited_bits > limit.max_bits) reason = V02ReasonCode::BudgetBitsExhausted;
	else if (dst.propagation_events > limit.max_events) reason = V02ReasonCode::BudgetEventsExhausted;
	else return true;
	return false;
}

int append_proofs(std::vector<ProofRecord> &dst, const EntryAnalysisResult &src, int local_id)
{
	const int base = GetSize(dst);
	for (ProofRecord record : src.proof_records) {
		record.proof_id += base;
		for (int &parent : record.parent_proof_ids)
			if (parent >= 0) parent += base;
		dst.push_back(record);
	}
	return local_id < 0 ? -1 : local_id + base;
}

CellSnapshot snapshot_cell(const RTLIL::Cell *cell)
{
	CellSnapshot result;
	result.cell_id = cell->name.str();
	result.type = cell->type;
	for (const auto &conn : cell->connections())
		result.ports.push_back({conn.first, conn.second});
	for (const auto &param : cell->parameters)
		result.parameters.push_back({param.first, param.second});
	return result;
}

bool same_snapshot(const CellSnapshot &a, const CellSnapshot &b)
{
	if (a.cell_id != b.cell_id || a.type != b.type || a.ports.size() != b.ports.size() ||
			a.parameters.size() != b.parameters.size()) return false;
	for (std::size_t i = 0; i < a.ports.size(); ++i)
		if (a.ports[i].port != b.ports[i].port || a.ports[i].signal != b.ports[i].signal) return false;
	for (std::size_t i = 0; i < a.parameters.size(); ++i)
		if (a.parameters[i].parameter != b.parameters[i].parameter || a.parameters[i].value != b.parameters[i].value) return false;
	return true;
}

class Index {
public:
	RTLIL::Module *module;
	SigMap sigmap;
	NewCellTypes celltypes;
	std::map<RTLIL::SigBit, std::vector<Driver>, BitLess> drivers;
	std::map<RTLIL::SigBit, std::vector<FanoutUseSnapshot>, BitLess> uses;

	explicit Index(const RTLIL::Module *m) : module(const_cast<RTLIL::Module *>(m)), sigmap(module)
	{
		celltypes.setup(module->design);
		for (RTLIL::Cell *cell : module->cells()) {
			for (const auto &conn : cell->connections()) {
				bool known = celltypes.cell_known(cell->type);
				bool output = known && celltypes.cell_output(cell->type, conn.first);
				bool input = known && celltypes.cell_input(cell->type, conn.first);
				if (!known) {
					output = conn.first.in(ID::Y, ID::Q);
					input = !output;
				}
				for (int i = 0; i < GetSize(conn.second); ++i) {
					RTLIL::SigBit bit = sigmap(conn.second[i]);
					if (!bit.wire) continue;
					if (output) drivers[bit].push_back({cell, conn.first, i});
					if (input) uses[bit].push_back({cell->name.str(), conn.first, i, bit});
				}
			}
		}
		for (auto &item : uses)
			std::sort(item.second.begin(), item.second.end(), [](const FanoutUseSnapshot &a, const FanoutUseSnapshot &b) {
				return std::tie(a.cell_id, a.port, a.port_bit) < std::tie(b.cell_id, b.port, b.port_bit);
			});
	}

	std::vector<FanoutUseSnapshot> fanout(RTLIL::SigBit bit) const
	{
		bit = sigmap(bit);
		auto it = uses.find(bit);
		return it == uses.end() ? std::vector<FanoutUseSnapshot>() : it->second;
	}
};

bool same_fanout(const std::vector<FanoutUseSnapshot> &a, const std::vector<FanoutUseSnapshot> &b)
{
	auto lhs = a, rhs = b;
	auto less = [](const FanoutUseSnapshot &x, const FanoutUseSnapshot &y) {
		return std::tie(x.cell_id, x.port, x.port_bit) < std::tie(y.cell_id, y.port, y.port_bit);
	};
	std::sort(lhs.begin(), lhs.end(), less);
	std::sort(rhs.begin(), rhs.end(), less);
	if (lhs.size() != rhs.size()) return false;
	for (std::size_t i = 0; i < lhs.size(); ++i)
		if (lhs[i].cell_id != rhs[i].cell_id || lhs[i].port != rhs[i].port ||
				lhs[i].port_bit != rhs[i].port_bit || lhs[i].canonical_bit != rhs[i].canonical_bit) return false;
	return true;
}

bool source_and_constant(const RTLIL::Cell *cell, SigMap &sigmap, RTLIL::SigSpec &source,
		RTLIL::Const &constant)
{
	RTLIL::SigSpec a = sigmap(cell->getPort(ID::A));
	RTLIL::SigSpec b = sigmap(cell->getPort(ID::B));
	auto is_const = [](const RTLIL::SigSpec &sig) {
		for (RTLIL::SigBit bit : sig) if (bit.wire != nullptr || !binary(bit.data)) return false;
		return true;
	};
	if (is_const(a) == is_const(b)) return false;
	RTLIL::SigSpec c = is_const(a) ? a : b;
	source = is_const(a) ? b : a;
	constant = c.as_const();
	return true;
}

class ReadonlyGuardedConeAnalyzer final : public GuardedConeAnalyzer {
public:
	GuardedConeProof prove(const GuardedConeProofRequest &r) const override
	{
		GuardedConeProof out;
		out.module = r.module_id;
		out.guard_sink_cell = r.guard_sink_cell_id;
		out.guard_facts = r.guard_facts;
		out.source_value = r.source_value;
		out.source_width = r.source_width;
		out.removed_bit_indices = r.removed_bit_indices;
		out.affected_cells = r.affected_cells;
		for (const GuardFact &fact : r.guard_facts)
			if (fact.value == RTLIL::State::Sx || fact.value == RTLIL::State::Sz || fact.value == RTLIL::State::Sa) {
				out.status = V02ProofStatus::Unknown;
				out.reason_code = V02ReasonCode::GuardXz;
				return out;
			}
		if (!validate(r)) return out;
		Index index(r.module);
		std::set<std::string> affected(r.affected_cells.begin(), r.affected_cells.end());
		if (!guard_dominates(r, index)) {
			out.status = V02ProofStatus::Unknown;
			out.reason_code = V02ReasonCode::GuardNotDominating;
			return out;
		}

		bool all_comparators = true;
		for (const ComparatorNarrowingRequest &request : r.comparators) {
			ComparatorProofConclusion result;
			result.cell_id = request.cell_id;
			result.removed_bit_indices = request.removed_bit_indices;
			RTLIL::SigSpec source;
			RTLIL::Const constant;
			if (!request.cell->hasParam(ID::A_WIDTH) || !request.cell->hasParam(ID::B_WIDTH) ||
					request.cell->getParam(ID::A_WIDTH).as_int() != GetSize(request.cell->getPort(ID::A)) ||
					request.cell->getParam(ID::B_WIDTH).as_int() != GetSize(request.cell->getPort(ID::B)) ||
					GetSize(request.cell->getPort(ID::A)) != GetSize(request.cell->getPort(ID::B)) ||
					!source_and_constant(request.cell, index.sigmap, source, constant) ||
					source != index.sigmap(request.source_snapshot) || source != index.sigmap(r.source_value)) {
				result.reason_code = V02ReasonCode::ComparisonWidthUnknown;
				all_comparators = false;
				out.comparator_cells.push_back(result);
				continue;
			}
			result.comparator_constant = constant;
			result.full_width = GetSize(source);
			result.reduced_width = result.full_width - GetSize(request.removed_bit_indices);
			result.comparator_full_fanout_snapshot = index.fanout(request.cell->getPort(ID::Y)[0]);
			out.comparator_constants.push_back(constant);
			out.comparator_full_fanout_snapshot.push_back(result.comparator_full_fanout_snapshot);
			if (constant != request.constant_snapshot || request.removed_bit_indices != r.removed_bit_indices ||
					!same_fanout(result.comparator_full_fanout_snapshot, request.full_fanout_snapshot)) {
				result.reason_code = V02ReasonCode::SnapshotMismatch;
				all_comparators = false;
			} else if (!request.cell->hasParam(ID::A_SIGNED) || !request.cell->hasParam(ID::B_SIGNED)) {
				result.reason_code = V02ReasonCode::ComparisonSignednessUnknown;
				all_comparators = false;
			} else {
				result.a_signed = request.cell->getParam(ID::A_SIGNED).as_bool();
				result.b_signed = request.cell->getParam(ID::B_SIGNED).as_bool();
				if (result.a_signed != result.b_signed) {
					result.reason_code = V02ReasonCode::ComparisonSignednessUnknown;
					all_comparators = false;
					out.per_comparator_equivalent_under_guard.push_back(false);
					out.comparator_cells.push_back(result);
					continue;
				}
				EntryAnalysisResult facts = analyze_guarded_values(r.module, source, r.guard_facts, r.budget);
				V02ReasonCode usage_reason = V02ReasonCode::Proved;
				if (!add_usage(out.usage, facts.usage, r.budget, usage_reason)) {
					out.status = V02ProofStatus::BudgetExhausted;
					out.reason_code = usage_reason;
					return out;
				}
				if (facts.status != EntryStatus::Normal) {
					out.status = entry_status(facts);
					out.reason_code = entry_reason(facts);
					return out;
				}
				bool equivalent = true;
				std::set<int> unique;
				for (int removed : request.removed_bit_indices) {
					if (removed < 0 || removed >= GetSize(source) || !unique.insert(removed).second) { equivalent = false; break; }
					const BitConclusion *found = nullptr;
					for (const BitConclusion &bit : facts.bits) if (bit.bit_index == removed) found = &bit;
					FactValue expected = constant[removed] == RTLIL::State::S1 ? FactValue::One : FactValue::Zero;
					if (!found || found->value != expected) equivalent = false;
					else result.proof_ids.push_back(append_proofs(out.proof_records, facts, found->proof_id));
				}
				result.equivalent_under_guard = equivalent;
				result.reason_code = equivalent ? V02ReasonCode::Proved : V02ReasonCode::FullReducedCompareNotEquivalent;
				if (!equivalent) all_comparators = false;
			}
			for (const FanoutUseSnapshot &use : result.comparator_full_fanout_snapshot)
				if (!affected.count(use.cell_id)) result.comparator_must_be_preserved = true;
			if (std::all_of(result.comparator_full_fanout_snapshot.begin(), result.comparator_full_fanout_snapshot.end(),
					[&](const FanoutUseSnapshot &use) { return affected.count(use.cell_id); }))
				result.comparator_must_be_preserved = false;
			out.per_comparator_equivalent_under_guard.push_back(result.equivalent_under_guard);
			out.comparator_cells.push_back(result);
		}

		bool all_data = true;
		for (const GuardedDataBitRequest &item : r.folded_data_bits) {
			GuardedDataBitConclusion result;
			result.result_bit_index = item.result_bit_index;
			result.source_bit = item.source_bit;
			if (!binary(item.replacement) || item.comparator_index < -1 || item.comparator_index >= GetSize(r.comparators)) {
				all_data = false;
				out.folded_data_bits.push_back(result);
				continue;
			}
			std::vector<GuardFact> guard = r.guard_facts;
			if (item.comparator_index >= 0)
				guard.push_back({r.comparators[item.comparator_index].cell->getPort(ID::Y)[0], RTLIL::State::S1});
			EntryAnalysisResult facts = analyze_guarded_values(r.module, RTLIL::SigSpec(item.source_bit), guard, r.budget);
			V02ReasonCode usage_reason = V02ReasonCode::Proved;
			if (!add_usage(out.usage, facts.usage, r.budget, usage_reason)) {
				out.status = V02ProofStatus::BudgetExhausted;
				out.reason_code = usage_reason;
				return out;
			}
			if (facts.status != EntryStatus::Normal) {
				out.status = entry_status(facts);
				out.reason_code = entry_reason(facts);
				return out;
			}
			const BitConclusion *bit = facts.bits.empty() ? nullptr : &facts.bits.front();
			FactValue expected = item.replacement == RTLIL::State::S1 ? FactValue::One : FactValue::Zero;
			if (bit && bit->value == expected) {
				result.status = expected == FactValue::One ? GuardedDataBitStatus::FoldedOne : GuardedDataBitStatus::FoldedZero;
				result.proof_id = append_proofs(out.proof_records, facts, bit->proof_id);
				result.reason_code = V02ReasonCode::Proved;
				out.proof_ids.push_back(result.proof_id);
			} else all_data = false;
			out.folded_data_bits.push_back(result);
		}

		bool all_preserved = true;
		std::set<int> result_bits;
		for (const GuardedDataBitRequest &item : r.folded_data_bits) result_bits.insert(item.result_bit_index);
		for (const PreservedVariableBitRequest &item : r.preserved_variable_bits) {
			GuardedDataBitConclusion result;
			result.result_bit_index = item.result_bit_index;
			result.source_bit = item.source_bit;
			if (item.result_bit_index < 0 || result_bits.count(item.result_bit_index) || item.source_bit.wire == nullptr) {
				all_preserved = false;
				result.reason_code = V02ReasonCode::PreservedBitMissing;
			} else {
				result_bits.insert(item.result_bit_index);
				result.status = GuardedDataBitStatus::PreservedVariable;
				result.reason_code = V02ReasonCode::Proved;
			}
			out.preserved_variable_bits.push_back(result);
		}
		out.proven_all_obligations = all_comparators && all_data && all_preserved &&
				!r.comparators.empty() && !r.folded_data_bits.empty();
		out.status = out.proven_all_obligations ? V02ProofStatus::Normal : V02ProofStatus::Unknown;
		out.reason_code = out.proven_all_obligations ? V02ReasonCode::Proved :
				(!all_preserved ? V02ReasonCode::PreservedBitMissing : !all_data ? V02ReasonCode::PartialDataFold :
				 V02ReasonCode::FullReducedCompareNotEquivalent);
		return out;
	}

private:
	static bool validate(const GuardedConeProofRequest &r)
	{
		if (!r.module || !r.guard_sink_cell || !exact_budget(r.budget) || r.source_width <= 0 ||
				GetSize(r.source_value) != r.source_width || r.module_id != r.module->name.str() ||
				r.guard_sink_cell_id != r.guard_sink_cell->name.str() ||
				r.guard_sink_cell->module != r.module || r.module->cell(r.guard_sink_cell->name) != r.guard_sink_cell ||
				r.guard_facts.empty()) return false;
		for (const GuardFact &fact : r.guard_facts) if (!fact.bit.wire || !binary(fact.value)) return false;
		for (const ComparatorNarrowingRequest &cmp : r.comparators)
			if (!cmp.cell || cmp.cell->module != r.module || cmp.cell_id != cmp.cell->name.str() ||
					!cmp.cell->type.in(ID($eq), ID($ne)) || !cmp.cell->hasPort(ID::Y) || GetSize(cmp.cell->getPort(ID::Y)) != 1)
				return false;
		return true;
	}

	static bool guard_dominates(const GuardedConeProofRequest &r, Index &index)
	{
		std::map<RTLIL::SigBit, RTLIL::State, BitLess> facts;
		for (const GuardFact &fact : r.guard_facts) {
			RTLIL::SigBit bit = index.sigmap(fact.bit);
			if (facts.count(bit) && facts[bit] != fact.value) return false;
			facts[bit] = fact.value;
		}
		std::set<RTLIL::Cell *> seen;
		std::set<RTLIL::SigBit, BitLess> consumed;
		std::function<void(RTLIL::SigBit)> walk = [&](RTLIL::SigBit bit) {
			bit = index.sigmap(bit);
			if (!bit.wire) return;
			auto found = index.drivers.find(bit);
			if (found == index.drivers.end() || found->second.size() != 1) return;
			RTLIL::Cell *cell = found->second.front().cell;
			if (!seen.insert(cell).second) return;
			if (cell->type == ID($mux) && cell->hasPort(ID::S) && GetSize(cell->getPort(ID::S)) == 1) {
				RTLIL::SigBit select = index.sigmap(cell->getPort(ID::S)[0]);
				auto known = facts.find(select);
				if (known != facts.end()) {
					consumed.insert(select);
					RTLIL::SigSpec chosen = cell->getPort(known->second == RTLIL::State::S1 ? ID::B : ID::A);
					for (RTLIL::SigBit input : chosen) walk(input);
					return;
				}
			}
			for (const auto &conn : cell->connections())
				if (index.celltypes.cell_input(cell->type, conn.first))
					for (RTLIL::SigBit input : conn.second) walk(input);
		};
		for (RTLIL::SigBit bit : r.guard_sink_cell->getPort(ID::Y)) walk(bit);
		for (const GuardFact &fact : r.guard_facts) if (!consumed.count(index.sigmap(fact.bit))) return false;
		for (const ComparatorNarrowingRequest &cmp : r.comparators) if (!seen.count(const_cast<RTLIL::Cell *>(cmp.cell))) return false;
		return true;
	}
};

struct TableNode {
	bool ok = false;
	V02ReasonCode reason = V02ReasonCode::TableExtractFailed;
	std::vector<RTLIL::SigBit> order_lsb_first;
	std::vector<RTLIL::Const> rows;
	std::set<std::string> cells;
};

class TableExtractor {
public:
	Index &index;
	BudgetTracker budget;
	std::set<RTLIL::SigBit, BitLess> bits;

	TableExtractor(Index &i, const AnalysisBudget &b) : index(i), budget(b.max_cells, b.max_bits, b.max_events) { }

	TableNode extract(const RTLIL::SigSpec &raw)
	{
		if (!budget.take_event()) return fail_budget();
		RTLIL::SigSpec sig = index.sigmap(raw);
		for (RTLIL::SigBit bit : sig)
			if (bit.wire && bits.insert(bit).second && !budget.take_bit()) return fail_budget();
		bool constant = true;
		for (RTLIL::SigBit bit : sig) constant &= bit.wire == nullptr;
		if (constant) {
			for (RTLIL::SigBit bit : sig) if (!binary(bit.data)) return {false, V02ReasonCode::TableXz};
			TableNode leaf; leaf.ok = true; leaf.reason = V02ReasonCode::Proved; leaf.rows.push_back(sig.as_const()); return leaf;
		}
		RTLIL::Cell *driver = nullptr;
		for (int i = 0; i < GetSize(sig); ++i) {
			auto found = index.drivers.find(sig[i]);
			if (found == index.drivers.end()) return {false, V02ReasonCode::TableIncomplete};
			if (found->second.size() != 1) return {false, V02ReasonCode::MultipleDriver};
			if (!driver) driver = found->second.front().cell;
			if (driver != found->second.front().cell || found->second.front().offset != i) return {false, V02ReasonCode::TableExtractFailed};
		}
		if (!driver || driver->type != ID($mux) || !driver->hasPort(ID::S) || GetSize(driver->getPort(ID::S)) != 1 ||
				GetSize(driver->getPort(ID::A)) != GetSize(sig) || GetSize(driver->getPort(ID::B)) != GetSize(sig))
			return {false, V02ReasonCode::TableExtractFailed};
		if (!budget.take_cell()) return fail_budget();
		TableNode left = extract(driver->getPort(ID::A));
		if (!left.ok) return left;
		TableNode right = extract(driver->getPort(ID::B));
		if (!right.ok) return right;
		if (left.order_lsb_first.size() != right.order_lsb_first.size()) return {false, V02ReasonCode::TableIncomplete};
		for (std::size_t i = 0; i < left.order_lsb_first.size(); ++i)
			if (index.sigmap(left.order_lsb_first[i]) != index.sigmap(right.order_lsb_first[i]))
				return {false, V02ReasonCode::AddressOrderUnknown};
		TableNode out; out.ok = true; out.reason = V02ReasonCode::Proved;
		out.order_lsb_first = left.order_lsb_first;
		out.order_lsb_first.push_back(index.sigmap(driver->getPort(ID::S)[0]));
		out.rows = left.rows; out.rows.insert(out.rows.end(), right.rows.begin(), right.rows.end());
		out.cells = left.cells; out.cells.insert(right.cells.begin(), right.cells.end()); out.cells.insert(driver->name.str());
		return out;
	}

	ResourceUsage usage() const { return {budget.cells(), budget.bits(), budget.events()}; }
	TableNode fail_budget() const { return {false, budget_code(budget.exhausted())}; }
};

bool is_sequential_type(RTLIL::IdString type)
{
	std::string s = type.str();
	return s.find("dff") != std::string::npos || s.find("DFF") != std::string::npos ||
		s.find("latch") != std::string::npos || s.find("LATCH") != std::string::npos;
}

bool influences_sink(Index &index, RTLIL::SigBit root, const RTLIL::Cell *sink)
{
	std::deque<RTLIL::SigBit> queue;
	std::set<RTLIL::SigBit, BitLess> seen;
	queue.push_back(index.sigmap(root));
	while (!queue.empty()) {
		RTLIL::SigBit bit = queue.front(); queue.pop_front();
		if (!seen.insert(bit).second) continue;
		for (const FanoutUseSnapshot &use : index.fanout(bit)) {
			RTLIL::Cell *cell = index.module->cell(RTLIL::IdString(use.cell_id));
			if (!cell) continue;
			if (cell == sink && use.port == ID::D) return true;
			if (is_sequential_type(cell->type)) continue;
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

class ReadonlyConstantTableAnalyzer final : public ConstantTableAnalyzer {
public:
	ConstantTableProof prove(const ConstantTableProofRequest &r) const override
	{
		ConstantTableProof out;
		out.module = r.module_id;
		out.root_outputs = r.root_outputs;
		out.address_bits_lsb_first = r.address_bits_lsb_first;
		out.output_bits_lsb_first = r.output_bits_lsb_first;
		out.address_width = GetSize(r.address_bits_lsb_first);
		out.output_width = GetSize(r.output_bits_lsb_first);
		out.source_cells = r.source_cells;
		if (!r.module || r.module_id != r.module->name.str() || !exact_budget(r.budget) ||
				r.address_bits_lsb_first.empty() || r.output_bits_lsb_first.empty() ||
				GetSize(r.root_outputs) != GetSize(r.output_bits_lsb_first) || GetSize(r.address_bits_lsb_first) >= 31) {
			out.status = V02ProofStatus::InvalidRequest; out.reason_code = V02ReasonCode::InvalidRequest; return out;
		}
		Index index(r.module);
		std::set<RTLIL::SigBit, BitLess> unique_address;
		for (RTLIL::SigBit bit : r.address_bits_lsb_first)
			if (!bit.wire || !unique_address.insert(index.sigmap(bit)).second) {
				out.reason_code = V02ReasonCode::AddressOrderUnknown; return out;
			}
		for (int i = 0; i < GetSize(r.root_outputs); ++i)
			if (index.sigmap(r.root_outputs[i]) != index.sigmap(r.output_bits_lsb_first[i])) {
				out.reason_code = V02ReasonCode::OutputOrderUnknown; return out;
			}
		TableExtractor extractor(index, r.budget);
		TableNode table = extractor.extract(r.root_outputs);
		out.usage = extractor.usage();
		if (!table.ok) {
			out.reason_code = table.reason;
			out.status = table.reason == V02ReasonCode::BudgetCellsExhausted || table.reason == V02ReasonCode::BudgetBitsExhausted ||
				table.reason == V02ReasonCode::BudgetEventsExhausted ? V02ProofStatus::BudgetExhausted : V02ProofStatus::Unknown;
			return out;
		}
		if (table.order_lsb_first.size() != r.address_bits_lsb_first.size()) {
			out.reason_code = V02ReasonCode::TableIncomplete; return out;
		}
		for (std::size_t i = 0; i < table.order_lsb_first.size(); ++i)
			if (index.sigmap(table.order_lsb_first[i]) != index.sigmap(r.address_bits_lsb_first[i])) {
				out.reason_code = V02ReasonCode::AddressOrderUnknown; return out;
			}
		std::set<std::string> requested(r.source_cells.begin(), r.source_cells.end());
		if (requested != table.cells) { out.reason_code = V02ReasonCode::SnapshotMismatch; return out; }
		const std::size_t expected_rows = std::size_t(1) << r.address_bits_lsb_first.size();
		if (table.rows.size() != expected_rows) { out.reason_code = V02ReasonCode::TableIncomplete; return out; }
		out.truth_table = table.rows;
		out.complete_binary_domain = true;
		std::set<std::pair<const RTLIL::Cell *, int>> discovered_sinks, requested_sinks;
		for (int bit = 0; bit < out.output_width; ++bit)
			for (RTLIL::Cell *cell : index.module->cells())
				if (is_sequential_type(cell->type) && influences_sink(index, r.root_outputs[bit], cell))
					discovered_sinks.insert({cell, bit});
		for (const SequentialSinkRequest &sink_request : r.sequential_sinks)
			for (int bit : sink_request.root_output_bit_indices) {
				if (bit < 0 || bit >= out.output_width) {
					out.reason_code = V02ReasonCode::SequentialBoundaryChanged; return out;
				}
				requested_sinks.insert({sink_request.cell, bit});
			}
		if (discovered_sinks != requested_sinks) {
			out.reason_code = V02ReasonCode::ResetEnableHoldUnknown; return out;
		}
		std::vector<int> covered(out.output_width, 0);
		for (const SequentialSinkRequest &sink_request : r.sequential_sinks) {
			if (!sink_request.cell || sink_request.cell->module != r.module || sink_request.cell_id != sink_request.cell->name.str() ||
					!is_sequential_type(sink_request.cell->type) || !same_snapshot(sink_request.snapshot, snapshot_cell(sink_request.cell))) {
				out.reason_code = V02ReasonCode::SequentialBoundaryChanged; return out;
			}
			SequentialSinkSnapshot sink;
			sink.cell_id = sink_request.cell_id; sink.cell = sink_request.snapshot; sink.root_output_bit_indices = sink_request.root_output_bit_indices;
			for (int bit : sink_request.root_output_bit_indices) {
				if (bit < 0 || bit >= out.output_width || ++covered[bit] != 1 || !influences_sink(index, r.root_outputs[bit], sink_request.cell)) {
					out.reason_code = V02ReasonCode::SequentialBoundaryChanged; return out;
				}
			}
			out.sequential_sinks.push_back(sink);
		}
		if (std::any_of(covered.begin(), covered.end(), [](int count) { return count != 1; })) {
			out.reason_code = V02ReasonCode::ResetEnableHoldUnknown; return out;
		}
		out.reset_enable_hold_contract.no_cross_dff_propagation = true;
		out.reset_enable_hold_contract.reset_preserved = true;
		out.reset_enable_hold_contract.enable_preserved = true;
		out.reset_enable_hold_contract.hold_preserved = true;
		out.reset_enable_hold_contract.proof_ids.push_back("sequential-cell-full-snapshot");
		out.proof_ids.reserve(expected_rows);
		for (std::size_t row = 0; row < expected_rows; ++row)
			out.proof_ids.push_back("table-row-" + std::to_string(row));
		out.proven_all_rows = true;
		out.status = V02ProofStatus::Normal;
		out.reason_code = V02ReasonCode::Proved;
		return out;
	}
};

} // namespace

const char *v02_reason_code_name(V02ReasonCode code)
{
	static const char *names[] = {
		"proved", "guard_not_dominating", "guard_unknown", "guard_xz",
		"comparison_width_unknown", "comparison_signedness_unknown", "comparator_fanout_uncovered",
		"full_reduced_compare_not_equivalent", "data_bit_unknown", "partial_data_fold", "preserved_bit_missing",
		"table_extract_failed", "address_order_unknown", "output_order_unknown", "table_incomplete",
		"table_value_conflict", "table_xz", "multiple_driver", "sequential_boundary_changed",
		"reset_enable_hold_unknown", "fact_conflict", "budget_cells_exhausted", "budget_bits_exhausted",
		"budget_events_exhausted", "sat_counterexample", "sat_unknown", "sat_timeout", "invalid_request",
		"snapshot_mismatch"
	};
	int value = int(code);
	return value >= 0 && value < int(sizeof(names) / sizeof(names[0])) ? names[value] : "invalid_request";
}

std::unique_ptr<GuardedConeAnalyzer> make_guarded_cone_analyzer()
{
	return std::make_unique<ReadonlyGuardedConeAnalyzer>();
}

std::unique_ptr<ConstantTableAnalyzer> make_constant_table_analyzer()
{
	return std::make_unique<ReadonlyConstantTableAnalyzer>();
}

} // namespace PmuxOpt

YOSYS_NAMESPACE_END
