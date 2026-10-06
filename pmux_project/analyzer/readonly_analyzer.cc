#include "readonly_analyzer.h"
#include "budget_tracker.h"
#include "guarded_fact_analysis.h"

#include "kernel/celltypes.h"
#include "kernel/consteval.h"
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
		if ((a.wire == nullptr) != (b.wire == nullptr))
			return a.wire == nullptr;
		if (a.wire == nullptr)
			return int(a.data) < int(b.data);
		if (a.wire->name != b.wire->name)
			return a.wire->name.str() < b.wire->name.str();
		return a.offset < b.offset;
	}
};

struct CellLess {
	bool operator()(const RTLIL::Cell *a, const RTLIL::Cell *b) const
	{
		return a->name.str() < b->name.str();
	}
};

struct Driver {
	RTLIL::Cell *cell = nullptr;
	RTLIL::IdString port;
	int offset = 0;
};

struct Fact {
	FactValue value = FactValue::Unknown;
	int proof_id = -1;
	int depth = 0;
};

static RTLIL::State fact_state(FactValue value)
{
	return value == FactValue::One ? RTLIL::State::S1 : RTLIL::State::S0;
}

static FactValue state_fact(RTLIL::State state)
{
	if (state == RTLIL::State::S0)
		return FactValue::Zero;
	if (state == RTLIL::State::S1)
		return FactValue::One;
	return FactValue::Unknown;
}

static bool is_binary_state(RTLIL::State state)
{
	return state == RTLIL::State::S0 || state == RTLIL::State::S1;
}

static FactValue invert(FactValue value)
{
	if (value == FactValue::Zero)
		return FactValue::One;
	if (value == FactValue::One)
		return FactValue::Zero;
	return FactValue::Unknown;
}

static bool is_sequential(RTLIL::IdString type)
{
	const std::string name = type.str();
	return name.find("dff") != std::string::npos ||
		name.find("DFF") != std::string::npos ||
		name.find("latch") != std::string::npos ||
		name.find("LATCH") != std::string::npos;
}

static bool is_memory(RTLIL::IdString type)
{
	const std::string name = type.str();
	return name.rfind("$mem", 0) == 0 || name.find("MEM") != std::string::npos;
}

static bool supported_cell(RTLIL::IdString type)
{
	return type.in(ID($not), ID($and), ID($or), ID($mux), ID($logic_not),
			ID($logic_and), ID($logic_or), ID($reduce_and), ID($reduce_or),
			ID($reduce_bool), ID($eq), ID($ne));
}

static std::string budget_reason(BudgetKind kind)
{
	switch (kind) {
	case BudgetKind::Cells: return "budget_cells_exhausted";
	case BudgetKind::Bits: return "budget_bits_exhausted";
	case BudgetKind::Events: return "budget_events_exhausted";
	case BudgetKind::None: break;
	}
	return "";
}

class ReadonlyEntryAnalyzer final : public EntryAnalyzer
{
public:
	EntryAnalysisResult analyze(const EntryRequest &request) const override
	{
		EntryAnalysisResult invalid;
		std::string validation_error;
		if (!validate_request(request, validation_error)) {
			invalid.status = EntryStatus::InvalidRequest;
			invalid.reason_code = validation_error;
			return invalid;
		}

		return analyze_guarded_values(request.module, request.data,
				request.guard, request.budget);
	}

private:
	static bool exact_budget(const AnalysisBudget &budget)
	{
		return budget.max_cells == 512 && budget.max_bits == 8192 &&
			budget.max_events == 262144;
	}

	static bool validate_request(const EntryRequest &r, std::string &reason)
	{
		if (r.module == nullptr || r.cell == nullptr) {
			reason = "invalid_null_object";
			return false;
		}
		if (r.cell->module != r.module || r.module->cell(r.cell->name) != r.cell) {
			reason = "invalid_cell_membership";
			return false;
		}
		if (r.module_id != r.module->name.str() || r.cell_id != r.cell->name.str()) {
			reason = "invalid_stable_id";
			return false;
		}
		if (!exact_budget(r.budget)) {
			reason = "invalid_budget";
			return false;
		}
		if (r.width <= 0 || GetSize(r.data) != r.width) {
			reason = "invalid_entry_width";
			return false;
		}

		const bool mux = r.cell->type == ID($mux);
		const bool pmux = r.cell->type == ID($pmux);
		if (!mux && !pmux) {
			reason = "invalid_cell_type";
			return false;
		}
		if (!r.cell->hasParam(ID::WIDTH) || !r.cell->hasPort(ID::A) ||
				!r.cell->hasPort(ID::B) || !r.cell->hasPort(ID::S) ||
				!r.cell->hasPort(ID::Y)) {
			reason = "invalid_cell_shape";
			return false;
		}
		const int width = r.cell->getParam(ID::WIDTH).as_int();
		if (width != r.width || GetSize(r.cell->getPort(ID::A)) != width ||
				GetSize(r.cell->getPort(ID::Y)) != width) {
			reason = "invalid_cell_width";
			return false;
		}

		RTLIL::SigSpec expected_data;
		RTLIL::SigSpec expected_port;
		std::vector<GuardFact> expected_guard;
		if (mux) {
			if (GetSize(r.cell->getPort(ID::B)) != width || GetSize(r.cell->getPort(ID::S)) != 1 ||
					(r.entry_kind != EntryKind::MuxA && r.entry_kind != EntryKind::MuxB)) {
				reason = "invalid_mux_shape";
				return false;
			}
			const bool use_b = r.entry_kind == EntryKind::MuxB;
			if (r.physical_port != (use_b ? PhysicalPort::B : PhysicalPort::A) ||
					r.branch_index != -1 || r.physical_offset != 0) {
				reason = "invalid_mux_entry";
				return false;
			}
			expected_port = r.cell->getPort(use_b ? ID::B : ID::A);
			expected_data = expected_port;
			expected_guard.push_back({r.cell->getPort(ID::S)[0],
					use_b ? RTLIL::State::S1 : RTLIL::State::S0});
		} else {
			if (!r.cell->hasParam(ID::S_WIDTH)) {
				reason = "invalid_pmux_shape";
				return false;
			}
			const int s_width = r.cell->getParam(ID::S_WIDTH).as_int();
			if (s_width <= 0 || GetSize(r.cell->getPort(ID::S)) != s_width ||
					GetSize(r.cell->getPort(ID::B)) != width * s_width ||
					(r.entry_kind != EntryKind::PmuxA && r.entry_kind != EntryKind::PmuxB)) {
				reason = "invalid_pmux_shape";
				return false;
			}
			const bool use_b = r.entry_kind == EntryKind::PmuxB;
			if (r.physical_port != (use_b ? PhysicalPort::B : PhysicalPort::A)) {
				reason = "invalid_pmux_port";
				return false;
			}
			if (use_b) {
				if (r.branch_index < 0 || r.branch_index >= s_width ||
						r.physical_offset != r.branch_index * width) {
					reason = "invalid_pmux_branch";
					return false;
				}
				expected_port = r.cell->getPort(ID::B);
				expected_data = expected_port.extract(r.physical_offset, width);
				expected_guard.push_back({r.cell->getPort(ID::S)[r.branch_index], RTLIL::State::S1});
			} else {
				if (r.branch_index != -1 || r.physical_offset != 0) {
					reason = "invalid_pmux_default";
					return false;
				}
				expected_port = r.cell->getPort(ID::A);
				expected_data = expected_port;
				for (int i = 0; i < s_width; ++i)
					expected_guard.push_back({r.cell->getPort(ID::S)[i], RTLIL::State::S0});
			}
		}

		if (r.physical_port_snapshot != expected_port) {
			reason = "invalid_snapshot";
			return false;
		}
		if (r.data != expected_data) {
			reason = "invalid_data_slice";
			return false;
		}
		if (r.guard.size() != expected_guard.size()) {
			reason = "invalid_guard_size";
			return false;
		}
		std::vector<bool> used(r.guard.size(), false);
		for (const GuardFact &expected : expected_guard) {
			bool found = false;
			for (std::size_t i = 0; i < r.guard.size(); ++i) {
				if (!used[i] && r.guard[i].bit == expected.bit && r.guard[i].value == expected.value) {
					used[i] = true;
					found = true;
					break;
				}
			}
			if (!found) {
				reason = "invalid_guard_fact";
				return false;
			}
		}
		return true;
	}

	public:
	class Analysis {
	public:
		Analysis(const RTLIL::Module *module, const RTLIL::SigSpec &data,
				const std::vector<GuardFact> &guard, const AnalysisBudget &budget) :
			module_(const_cast<RTLIL::Module *>(module)), data_(data), guard_(guard),
			sigmap_(module_), celltypes_(), budget_(budget.max_cells,
					budget.max_bits, budget.max_events)
		{
			celltypes_.setup(module_->design);
			build_indexes();
		}

		EntryAnalysisResult execute()
		{
			for (int i = 0; i < GetSize(data_); ++i) {
				const RTLIL::SigBit original = data_[i];
				if (original.wire == nullptr && is_binary_state(original.data))
					continue;
				targets_.push_back(i);
				if (original.wire == nullptr) {
					target_reason_[i] = "original_xz";
					continue;
				}
				collect_cone(sigmap_(original), i);
			}
			const std::vector<GuardFact> guards = sorted_guards();
			for (const GuardFact &guard : guards)
				collect_cone(sigmap_(guard.bit), -1);

			finalize_adjacency();
			seed_constants();
			for (const GuardFact &guard : guards)
				add_fact(sigmap_(guard.bit), state_fact(guard.value), "guard", {}, "");

			while (!queue_.empty() && !conflict_ && budget_.exhausted() == BudgetKind::None) {
				if (!budget_.take_event())
					break;
				RTLIL::SigBit bit = queue_.front();
				queue_.pop_front();
				auto it = adjacency_.find(bit);
				if (it == adjacency_.end())
					continue;
				for (RTLIL::Cell *cell : it->second) {
					if (!budget_.take_event())
						break;
					process_cell(cell);
					if (conflict_ || budget_.exhausted() != BudgetKind::None)
						break;
				}
			}

			EntryAnalysisResult result;
			result.proof_records = proofs_;
			result.usage.visited_cells = budget_.cells();
			result.usage.visited_bits = budget_.bits();
			result.usage.propagation_events = budget_.events();
			if (conflict_) {
				result.status = EntryStatus::FactConflict;
				result.reason_code = "fact_conflict";
			} else if (budget_.exhausted() != BudgetKind::None) {
				result.status = EntryStatus::BudgetExhausted;
				result.reason_code = budget_reason(budget_.exhausted());
			} else {
				result.status = EntryStatus::Normal;
				result.reason_code = "fixed_point";
			}

			for (int index : targets_) {
				BitConclusion conclusion;
				conclusion.bit_index = index;
				if (conflict_) {
					conclusion.reason_code = "fact_conflict";
				} else if (data_[index].wire == nullptr) {
					conclusion.reason_code = "original_xz";
				} else {
					RTLIL::SigBit bit = sigmap_(data_[index]);
					auto fact = facts_.find(bit);
					if (fact != facts_.end()) {
						conclusion.value = fact->second.value;
						conclusion.proof_id = fact->second.proof_id;
						conclusion.reason_code = "proved";
					} else if (budget_.exhausted() != BudgetKind::None) {
						conclusion.reason_code = budget_reason(budget_.exhausted());
					} else if (target_reason_.count(index)) {
						conclusion.reason_code = target_reason_.at(index);
					} else {
						conclusion.reason_code = "fixed_point_unknown";
					}
				}
				result.bits.push_back(conclusion);
			}
			return result;
		}

	private:
		RTLIL::Module *module_;
		RTLIL::SigSpec data_;
		std::vector<GuardFact> guard_;
		SigMap sigmap_;
		NewCellTypes celltypes_;
		BudgetTracker budget_;
		std::map<RTLIL::SigBit, std::vector<Driver>, BitLess> drivers_;
		std::map<RTLIL::SigBit, std::vector<RTLIL::Cell *>, BitLess> adjacency_;
		std::set<RTLIL::SigBit, BitLess> seen_bits_;
		std::set<RTLIL::Cell *, CellLess> cone_cells_;
		std::map<RTLIL::SigBit, Fact, BitLess> facts_;
		std::deque<RTLIL::SigBit> queue_;
		std::vector<ProofRecord> proofs_;
		std::vector<int> targets_;
		std::map<int, std::string> target_reason_;
		bool conflict_ = false;

		std::vector<GuardFact> sorted_guards() const
		{
			std::vector<GuardFact> guards = guard_;
			std::sort(guards.begin(), guards.end(), [](const GuardFact &a, const GuardFact &b) {
				BitLess less;
				if (less(a.bit, b.bit)) return true;
				if (less(b.bit, a.bit)) return false;
				return int(a.value) < int(b.value);
			});
			return guards;
		}

		void finalize_adjacency()
		{
			for (auto &item : adjacency_) {
				auto &cells = item.second;
				std::sort(cells.begin(), cells.end(), CellLess());
				cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
			}
		}

		void build_indexes()
		{
			std::vector<RTLIL::Cell *> cells = module_->cells();
			std::sort(cells.begin(), cells.end(), CellLess());
			for (RTLIL::Cell *cell : cells) {
				for (const auto &conn : cell->connections()) {
					bool output = celltypes_.cell_known(cell->type) && celltypes_.cell_output(cell->type, conn.first);
					if (!celltypes_.cell_known(cell->type))
						output = conn.first.in(ID::Y, ID::Q);
					if (!output)
						continue;
					for (int i = 0; i < GetSize(conn.second); ++i) {
						RTLIL::SigBit bit = sigmap_(conn.second[i]);
						if (bit.wire)
							drivers_[bit].push_back({cell, conn.first, i});
					}
				}
			}
		}

		bool touch_bit(RTLIL::SigBit bit)
		{
			bit = sigmap_(bit);
			if (bit.wire == nullptr || seen_bits_.count(bit))
				return true;
			if (!budget_.take_bit())
				return false;
			seen_bits_.insert(bit);
			return true;
		}

		std::string boundary_reason(RTLIL::Cell *cell) const
		{
			if (is_sequential(cell->type))
				return "sequential_boundary";
			if (is_memory(cell->type))
				return "memory_boundary";
			RTLIL::Module *type_module = module_->design ? module_->design->module(cell->type) : nullptr;
			if (type_module && type_module->get_blackbox_attribute())
				return "blackbox_boundary";
			return "unsupported_cell";
		}

		bool comparison_semantics_clear(RTLIL::Cell *cell) const
		{
			if (!cell->hasPort(ID::A) || !cell->hasPort(ID::B) || !cell->hasPort(ID::Y))
				return false;
			if (GetSize(cell->getPort(ID::Y)) != 1 ||
					GetSize(cell->getPort(ID::A)) != GetSize(cell->getPort(ID::B)))
				return false;
			bool a_signed = cell->hasParam(ID::A_SIGNED) && cell->getParam(ID::A_SIGNED).as_bool();
			bool b_signed = cell->hasParam(ID::B_SIGNED) && cell->getParam(ID::B_SIGNED).as_bool();
			return a_signed == b_signed;
		}

		void collect_cone(RTLIL::SigBit bit, int target)
		{
			if (budget_.exhausted() != BudgetKind::None)
				return;
			bit = sigmap_(bit);
			if (bit.wire == nullptr) {
				add_fact(bit, state_fact(bit.data), "constant", {}, "");
				return;
			}
			if (!touch_bit(bit))
				return;
			auto found = drivers_.find(bit);
			if (found == drivers_.end()) {
				if (target >= 0)
					target_reason_.emplace(target, bit.wire->port_input ? "primary_input" : "undriven");
				return;
			}
			if (found->second.size() != 1) {
				if (target >= 0)
					target_reason_[target] = "multiple_driver";
				return;
			}
			RTLIL::Cell *cell = found->second.front().cell;
			if (!supported_cell(cell->type)) {
				if (target >= 0)
					target_reason_[target] = boundary_reason(cell);
				return;
			}
			if (cell->type.in(ID($eq), ID($ne)) && !comparison_semantics_clear(cell)) {
				if (target >= 0)
					target_reason_[target] = "comparison_semantics_ambiguous";
				return;
			}
			if (!cone_cells_.count(cell)) {
				if (!budget_.take_cell())
					return;
				cone_cells_.insert(cell);
				for (const auto &conn : cell->connections()) {
					for (RTLIL::SigBit port_bit : sigmap_(conn.second)) {
						touch_bit(port_bit);
						adjacency_[port_bit].push_back(cell);
					}
				}
				for (const auto &conn : cell->connections()) {
					if (celltypes_.cell_input(cell->type, conn.first))
						for (RTLIL::SigBit input : sigmap_(conn.second))
							collect_cone(input, target);
				}
			}
		}

		void seed_constants()
		{
			for (RTLIL::Cell *cell : cone_cells_)
				for (const auto &conn : cell->connections())
					for (RTLIL::SigBit bit : sigmap_(conn.second))
						if (bit.wire == nullptr && is_binary_state(bit.data))
							add_fact(bit, state_fact(bit.data), "constant", {}, "");
		}

		FactValue value(RTLIL::SigBit bit) const
		{
			bit = sigmap_(bit);
			if (bit.wire == nullptr)
				return state_fact(bit.data);
			auto it = facts_.find(bit);
			return it == facts_.end() ? FactValue::Unknown : it->second.value;
		}

		int proof(RTLIL::SigBit bit) const
		{
			bit = sigmap_(bit);
			auto it = facts_.find(bit);
			return it == facts_.end() ? -1 : it->second.proof_id;
		}

		int depth(const std::vector<int> &parents) const
		{
			int result = 0;
			for (int id : parents)
				if (id >= 0 && id < GetSize(proofs_))
					result = std::max(result, fact_depth_.at(id));
			return parents.empty() ? 0 : result + 1;
		}

		bool add_fact(RTLIL::SigBit bit, FactValue val, const std::string &rule,
				const std::vector<int> &parents, const std::string &source)
		{
			if (val == FactValue::Unknown || conflict_ || budget_.exhausted() != BudgetKind::None)
				return false;
			bit = sigmap_(bit);
			if (!touch_bit(bit))
				return false;
			const int candidate_depth = depth(parents);
			auto existing = facts_.find(bit);
			if (existing != facts_.end() && existing->second.value == val &&
					candidate_depth >= existing->second.depth)
				return false;
			ProofRecord record;
			record.proof_id = GetSize(proofs_);
			record.target = bit;
			record.value = val;
			record.rule = rule;
			record.source_cell_id = source;
			record.parent_proof_ids = parents;
			for (int id : parents)
				if (id >= 0 && id < GetSize(proofs_))
					record.direct_premises.push_back({proofs_[id].target, fact_state(proofs_[id].value)});
			proofs_.push_back(record);
			fact_depth_.push_back(candidate_depth);

			auto it = facts_.find(bit);
			if (it != facts_.end()) {
				if (it->second.value != val) {
					conflict_ = true;
					return false;
				}
				if (candidate_depth < it->second.depth ||
						(candidate_depth == it->second.depth && record.proof_id < it->second.proof_id)) {
					it->second.proof_id = record.proof_id;
					it->second.depth = candidate_depth;
					queue_.push_back(bit);
					return true;
				}
				return false;
			}
			facts_[bit] = {val, record.proof_id, candidate_depth};
			queue_.push_back(bit);
			return true;
		}

		std::vector<int> parents(std::initializer_list<RTLIL::SigBit> bits) const
		{
			std::vector<int> ids;
			for (RTLIL::SigBit bit : bits) {
				int id = proof(bit);
				if (id >= 0)
					ids.push_back(id);
			}
			return ids;
		}

		void unary_relation(RTLIL::Cell *cell, RTLIL::SigBit a, RTLIL::SigBit y, bool inv,
				const std::string &rule)
		{
			FactValue av = value(a), yv = value(y);
			if (av != FactValue::Unknown)
				add_fact(y, inv ? invert(av) : av, rule + "_forward", parents({a}), cell->name.str());
			if (yv != FactValue::Unknown)
				add_fact(a, inv ? invert(yv) : yv, rule + "_backward", parents({y}), cell->name.str());
		}

		void process_bitwise(RTLIL::Cell *cell, bool is_and)
		{
			RTLIL::SigSpec a = sigmap_(cell->getPort(ID::A));
			RTLIL::SigSpec b = sigmap_(cell->getPort(ID::B));
			RTLIL::SigSpec y = sigmap_(cell->getPort(ID::Y));
			if (GetSize(a) != GetSize(b) || GetSize(a) != GetSize(y))
				return;
			for (int i = 0; i < GetSize(y); ++i) {
				FactValue av = value(a[i]), bv = value(b[i]), yv = value(y[i]);
				const std::string base = is_and ? "and" : "or";
				const FactValue decisive = is_and ? FactValue::Zero : FactValue::One;
				const FactValue identity = is_and ? FactValue::One : FactValue::Zero;
				if (av == decisive || bv == decisive)
					add_fact(y[i], decisive, base + "_decisive", parents({av == decisive ? a[i] : b[i]}), cell->name.str());
				if (av == identity && bv == identity)
					add_fact(y[i], identity, base + "_all_identity", parents({a[i], b[i]}), cell->name.str());
				if (yv == identity) {
					add_fact(a[i], identity, base + "_output_identity", parents({y[i]}), cell->name.str());
					add_fact(b[i], identity, base + "_output_identity", parents({y[i]}), cell->name.str());
				}
				if (yv == decisive && av == identity)
					add_fact(b[i], decisive, base + "_unit", parents({y[i], a[i]}), cell->name.str());
				if (yv == decisive && bv == identity)
					add_fact(a[i], decisive, base + "_unit", parents({y[i], b[i]}), cell->name.str());
			}
		}

		struct TruthInfo {
			bool known_zero = false;
			bool known_nonzero = false;
			int sole_unknown = -1;
		};

		TruthInfo truth(const RTLIL::SigSpec &sig) const
		{
			TruthInfo info;
			int unknowns = 0;
			for (int i = 0; i < GetSize(sig); ++i) {
				FactValue v = value(sig[i]);
				if (v == FactValue::One)
					info.known_nonzero = true;
				else if (v == FactValue::Unknown)
					++unknowns, info.sole_unknown = i;
			}
			info.known_zero = !info.known_nonzero && unknowns == 0;
			if (unknowns != 1)
				info.sole_unknown = -1;
			return info;
		}

		std::vector<int> all_known_parents(const RTLIL::SigSpec &sig) const
		{
			std::vector<int> ids;
			for (RTLIL::SigBit bit : sig) {
				int id = proof(bit);
				if (id >= 0)
					ids.push_back(id);
			}
			return ids;
		}

		std::vector<int> known_parents(const RTLIL::SigSpec &first, RTLIL::SigBit extra) const
		{
			std::vector<int> ids = all_known_parents(first);
			int id = proof(extra);
			if (id >= 0)
				ids.push_back(id);
			return ids;
		}

		std::vector<int> truth_parents(const RTLIL::SigSpec &sig, bool want_nonzero) const
		{
			std::vector<int> ids;
			for (RTLIL::SigBit bit : sig) {
				FactValue v = value(bit);
				if ((want_nonzero && v == FactValue::One) || (!want_nonzero && v == FactValue::Zero)) {
					int id = proof(bit);
					if (id >= 0)
						ids.push_back(id);
					if (want_nonzero)
						break;
				}
			}
			return ids;
		}

		void set_all(const RTLIL::SigSpec &sig, FactValue v, const std::string &rule,
				const std::vector<int> &parents, RTLIL::Cell *cell)
		{
			for (RTLIL::SigBit bit : sig)
				add_fact(bit, v, rule, parents, cell->name.str());
		}

		void process_reduce(RTLIL::Cell *cell, bool is_and)
		{
			RTLIL::SigSpec a = sigmap_(cell->getPort(ID::A));
			RTLIL::SigBit y = sigmap_(cell->getPort(ID::Y)[0]);
			TruthInfo t = truth(a);
			FactValue yv = value(y);
			if (is_and) {
				bool any_zero = false, all_one = true;
				for (RTLIL::SigBit bit : a) {
					any_zero |= value(bit) == FactValue::Zero;
					all_one &= value(bit) == FactValue::One;
				}
				if (any_zero)
					for (RTLIL::SigBit bit : a) if (value(bit) == FactValue::Zero) { add_fact(y, FactValue::Zero, "reduce_and_zero", parents({bit}), cell->name.str()); break; }
				if (all_one)
					add_fact(y, FactValue::One, "reduce_and_all_one", all_known_parents(a), cell->name.str());
				if (yv == FactValue::One)
					set_all(a, FactValue::One, "reduce_and_output_one", parents({y}), cell);
				if (yv == FactValue::Zero) {
					int unknown = -1; bool others_one = true;
					for (int i = 0; i < GetSize(a); ++i) {
						if (value(a[i]) == FactValue::Unknown) { if (unknown != -1) others_one = false; unknown = i; }
						else if (value(a[i]) != FactValue::One) others_one = false;
					}
					if (others_one && unknown >= 0)
						add_fact(a[unknown], FactValue::Zero, "reduce_and_unit", known_parents(a, y), cell->name.str());
				}
			} else {
				if (t.known_nonzero) {
					for (RTLIL::SigBit bit : a) if (value(bit) == FactValue::One) { add_fact(y, FactValue::One, "reduce_or_one", parents({bit}), cell->name.str()); break; }
				}
				if (t.known_zero)
					add_fact(y, FactValue::Zero, "reduce_or_all_zero", all_known_parents(a), cell->name.str());
				if (yv == FactValue::Zero)
					set_all(a, FactValue::Zero, "reduce_or_output_zero", parents({y}), cell);
				if (yv == FactValue::One && !t.known_nonzero && t.sole_unknown >= 0)
					add_fact(a[t.sole_unknown], FactValue::One, "reduce_or_unit", known_parents(a, y), cell->name.str());
			}
		}

		void force_nonzero_if_unique(const RTLIL::SigSpec &sig, const std::vector<int> &parent_ids,
				const std::string &rule, RTLIL::Cell *cell)
		{
			TruthInfo t = truth(sig);
			if (!t.known_nonzero && t.sole_unknown >= 0) {
				std::vector<int> ids = parent_ids;
				std::vector<int> known = all_known_parents(sig);
				ids.insert(ids.end(), known.begin(), known.end());
				add_fact(sig[t.sole_unknown], FactValue::One, rule, ids, cell->name.str());
			}
		}

		void process_logic(RTLIL::Cell *cell)
		{
			RTLIL::SigSpec a = sigmap_(cell->getPort(ID::A));
			RTLIL::SigBit y = sigmap_(cell->getPort(ID::Y)[0]);
			TruthInfo ta = truth(a);
			FactValue yv = value(y);
			if (cell->type == ID($logic_not)) {
				if (ta.known_zero) add_fact(y, FactValue::One, "logic_not_zero", all_known_parents(a), cell->name.str());
				if (ta.known_nonzero) add_fact(y, FactValue::Zero, "logic_not_nonzero", all_known_parents(a), cell->name.str());
				if (yv == FactValue::One) set_all(a, FactValue::Zero, "logic_not_output_one", parents({y}), cell);
				if (yv == FactValue::Zero) force_nonzero_if_unique(a, parents({y}), "logic_not_unit", cell);
				return;
			}
			RTLIL::SigSpec b = sigmap_(cell->getPort(ID::B));
			TruthInfo tb = truth(b);
			if (cell->type == ID($logic_and)) {
				if (ta.known_zero || tb.known_zero) add_fact(y, FactValue::Zero, "logic_and_zero",
						ta.known_zero ? all_known_parents(a) : all_known_parents(b), cell->name.str());
				if (ta.known_nonzero && tb.known_nonzero) {
					std::vector<int> ids = truth_parents(a, true);
					std::vector<int> rhs = truth_parents(b, true);
					ids.insert(ids.end(), rhs.begin(), rhs.end());
					add_fact(y, FactValue::One, "logic_and_one", ids, cell->name.str());
				}
				if (yv == FactValue::One) {
					force_nonzero_if_unique(a, parents({y}), "logic_and_left_unit", cell);
					force_nonzero_if_unique(b, parents({y}), "logic_and_right_unit", cell);
				}
				if (yv == FactValue::Zero && ta.known_nonzero) {
					std::vector<int> ids = parents({y});
					std::vector<int> known = truth_parents(a, true);
					ids.insert(ids.end(), known.begin(), known.end());
					set_all(b, FactValue::Zero, "logic_and_right_zero", ids, cell);
				}
				if (yv == FactValue::Zero && tb.known_nonzero) {
					std::vector<int> ids = parents({y});
					std::vector<int> known = truth_parents(b, true);
					ids.insert(ids.end(), known.begin(), known.end());
					set_all(a, FactValue::Zero, "logic_and_left_zero", ids, cell);
				}
			} else {
				if (ta.known_nonzero || tb.known_nonzero) add_fact(y, FactValue::One, "logic_or_one",
						ta.known_nonzero ? truth_parents(a, true) : truth_parents(b, true), cell->name.str());
				if (ta.known_zero && tb.known_zero) {
					std::vector<int> ids = all_known_parents(a);
					std::vector<int> rhs = all_known_parents(b);
					ids.insert(ids.end(), rhs.begin(), rhs.end());
					add_fact(y, FactValue::Zero, "logic_or_zero", ids, cell->name.str());
				}
				if (yv == FactValue::Zero) {
					set_all(a, FactValue::Zero, "logic_or_left_zero", parents({y}), cell);
					set_all(b, FactValue::Zero, "logic_or_right_zero", parents({y}), cell);
				}
				if (yv == FactValue::One && ta.known_zero) {
					std::vector<int> ids = parents({y});
					std::vector<int> known = all_known_parents(a);
					ids.insert(ids.end(), known.begin(), known.end());
					force_nonzero_if_unique(b, ids, "logic_or_right_unit", cell);
				}
				if (yv == FactValue::One && tb.known_zero) {
					std::vector<int> ids = parents({y});
					std::vector<int> known = all_known_parents(b);
					ids.insert(ids.end(), known.begin(), known.end());
					force_nonzero_if_unique(a, ids, "logic_or_left_unit", cell);
				}
			}
		}

		void process_mux(RTLIL::Cell *cell)
		{
			RTLIL::SigSpec a = sigmap_(cell->getPort(ID::A));
			RTLIL::SigSpec b = sigmap_(cell->getPort(ID::B));
			RTLIL::SigSpec s = sigmap_(cell->getPort(ID::S));
			RTLIL::SigSpec y = sigmap_(cell->getPort(ID::Y));
			if (GetSize(s) != 1 || GetSize(a) != GetSize(b) || GetSize(a) != GetSize(y))
				return;
			FactValue sv = value(s[0]);
			if (sv == FactValue::Unknown)
				return;
			RTLIL::SigSpec chosen = sv == FactValue::One ? b : a;
			const std::string rule = sv == FactValue::One ? "mux_b" : "mux_a";
			for (int i = 0; i < GetSize(y); ++i) {
				FactValue cv = value(chosen[i]), yv = value(y[i]);
				if (cv != FactValue::Unknown)
					add_fact(y[i], cv, rule + "_forward", parents({chosen[i], s[0]}), cell->name.str());
				if (yv != FactValue::Unknown)
					add_fact(chosen[i], yv, rule + "_backward", parents({y[i], s[0]}), cell->name.str());
			}
		}

		void process_compare(RTLIL::Cell *cell)
		{
			RTLIL::SigSpec a = sigmap_(cell->getPort(ID::A));
			RTLIL::SigSpec b = sigmap_(cell->getPort(ID::B));
			RTLIL::SigSpec y = sigmap_(cell->getPort(ID::Y));
			if (!comparison_semantics_clear(cell))
				return;
			FactValue yv = value(y[0]);
			const bool trigger = (cell->type == ID($eq) && yv == FactValue::One) ||
				(cell->type == ID($ne) && yv == FactValue::Zero);
			if (!trigger)
				return;
			auto fully_const = [](const RTLIL::SigSpec &sig) {
				for (RTLIL::SigBit bit : sig)
					if (bit.wire != nullptr || !is_binary_state(bit.data))
						return false;
				return true;
			};
			const bool a_const = fully_const(a), b_const = fully_const(b);
			if (a_const == b_const)
				return;
			RTLIL::SigSpec x = a_const ? b : a;
			RTLIL::SigSpec c = a_const ? a : b;
			std::set<RTLIL::SigBit, BitLess> unique_bits;
			for (int i = 0; i < GetSize(x); ++i)
				if (x[i].wire == nullptr || !unique_bits.insert(x[i]).second)
					return;
			std::vector<int> premise_ids = parents({y[0]});
			std::vector<int> constant_ids = all_known_parents(c);
			premise_ids.insert(premise_ids.end(), constant_ids.begin(), constant_ids.end());
			for (int i = 0; i < GetSize(x); ++i)
				add_fact(x[i], state_fact(c[i].data), cell->type == ID($eq) ? "eq_true_constant" : "ne_false_constant",
						premise_ids, cell->name.str());
		}

		void consteval_forward(RTLIL::Cell *cell)
		{
			std::vector<int> parent_ids;
			for (const auto &conn : cell->connections()) {
				if (!celltypes_.cell_input(cell->type, conn.first))
					continue;
				for (RTLIL::SigBit bit : sigmap_(conn.second)) {
					if (value(bit) == FactValue::Unknown)
						return;
					int id = proof(bit);
					if (id >= 0) parent_ids.push_back(id);
				}
			}
			ConstEval eval(module_);
			for (const auto &fact : facts_)
				if (fact.first.wire)
					eval.set(RTLIL::SigSpec(fact.first), RTLIL::Const(fact_state(fact.second.value), 1));
			RTLIL::SigSpec y = cell->getPort(ID::Y);
			RTLIL::SigSpec undef;
			if (!eval.eval(y, undef) || !y.is_fully_def())
				return;
			for (int i = 0; i < GetSize(y); ++i)
				add_fact(cell->getPort(ID::Y)[i], state_fact(y[i].data), "consteval_forward", parent_ids, cell->name.str());
		}

		void process_cell(RTLIL::Cell *cell)
		{
			if (!cone_cells_.count(cell))
				return;
			if (cell->type == ID($not)) {
				RTLIL::SigSpec a = sigmap_(cell->getPort(ID::A));
				RTLIL::SigSpec y = sigmap_(cell->getPort(ID::Y));
				if (GetSize(a) == GetSize(y)) for (int i = 0; i < GetSize(y); ++i) unary_relation(cell, a[i], y[i], true, "not");
			} else if (cell->type == ID($and)) process_bitwise(cell, true);
			else if (cell->type == ID($or)) process_bitwise(cell, false);
			else if (cell->type == ID($mux)) process_mux(cell);
			else if (cell->type.in(ID($logic_not), ID($logic_and), ID($logic_or))) process_logic(cell);
			else if (cell->type == ID($reduce_and)) process_reduce(cell, true);
			else if (cell->type.in(ID($reduce_or), ID($reduce_bool))) process_reduce(cell, false);
			else if (cell->type.in(ID($eq), ID($ne))) process_compare(cell);
			consteval_forward(cell);
		}

		std::vector<int> fact_depth_;
	};
};

} // namespace

EntryAnalysisResult analyze_guarded_values(const RTLIL::Module *module,
		const RTLIL::SigSpec &data, const std::vector<GuardFact> &guard,
		const AnalysisBudget &budget)
{
	ReadonlyEntryAnalyzer::Analysis run(module, data, guard, budget);
	return run.execute();
}

std::unique_ptr<EntryAnalyzer> make_readonly_entry_analyzer()
{
	return std::make_unique<ReadonlyEntryAnalyzer>();
}

} // namespace PmuxOpt

YOSYS_NAMESPACE_END
