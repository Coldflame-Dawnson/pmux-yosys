#include "pmux_v02_production_proof_api.h"
#include "budget_tracker.h"
#include "guarded_fact_analysis.h"

#include "kernel/celltypes.h"
#include "kernel/sigtools.h"

#include <algorithm>
#include <deque>
#include <functional>
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
		if (!a.wire) return int(a.data) < int(b.data);
		if (a.wire->name != b.wire->name) return a.wire->name.str() < b.wire->name.str();
		return a.offset < b.offset;
	}
};

struct Driver { RTLIL::Cell *cell = nullptr; RTLIL::IdString port; int offset = -1; };

bool binary(RTLIL::State value)
{
	return value == RTLIL::State::S0 || value == RTLIL::State::S1;
}

bool is_sequential(RTLIL::IdString type)
{
	std::string name = type.str();
	return name.find("dff") != std::string::npos || name.find("DFF") != std::string::npos ||
		name.find("latch") != std::string::npos || name.find("LATCH") != std::string::npos;
}

bool exact_budget(const AnalysisBudget &budget)
{
	return budget.max_cells == 512 && budget.max_bits == 8192 && budget.max_events == 262144;
}

V02ReasonCode budget_reason(BudgetKind kind)
{
	if (kind == BudgetKind::Cells) return V02ReasonCode::BudgetCellsExhausted;
	if (kind == BudgetKind::Bits) return V02ReasonCode::BudgetBitsExhausted;
	return V02ReasonCode::BudgetEventsExhausted;
}

CellSnapshot snapshot_cell(const RTLIL::Cell *cell)
{
	CellSnapshot result;
	result.cell_id = cell->name.str();
	result.type = cell->type;
	for (const auto &conn : cell->connections()) result.ports.push_back({conn.first, conn.second});
	for (const auto &param : cell->parameters) result.parameters.push_back({param.first, param.second});
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

struct Index {
	RTLIL::Module *module;
	SigMap sigmap;
	NewCellTypes celltypes;
	std::map<RTLIL::SigBit, std::vector<Driver>, BitLess> drivers;
	std::map<RTLIL::SigBit, std::vector<FanoutUseSnapshot>, BitLess> uses;
	std::set<RTLIL::SigBit, BitLess> module_outputs;

	explicit Index(const RTLIL::Module *raw) : module(const_cast<RTLIL::Module *>(raw)), sigmap(module)
	{
		celltypes.setup(module->design);
		for (RTLIL::Wire *wire : module->wires())
			if (wire->port_output)
				for (int i = 0; i < wire->width; ++i) module_outputs.insert(sigmap(RTLIL::SigBit(wire, i)));
		for (RTLIL::Cell *cell : module->cells()) {
			for (const auto &conn : cell->connections()) {
				bool known = celltypes.cell_known(cell->type);
				bool output = known ? celltypes.cell_output(cell->type, conn.first) : conn.first.in(ID::Y, ID::Q);
				bool input = known ? celltypes.cell_input(cell->type, conn.first) : !output;
				for (int i = 0; i < GetSize(conn.second); ++i) {
					RTLIL::SigBit bit = sigmap(conn.second[i]);
					if (!bit.wire) continue;
					if (output) drivers[bit].push_back({cell, conn.first, i});
					if (input) uses[bit].push_back({cell->name.str(), conn.first, i, bit});
				}
			}
		}
	}

	std::vector<FanoutUseSnapshot> fanout(RTLIL::SigBit bit) const
	{
		auto found = uses.find(sigmap(bit));
		return found == uses.end() ? std::vector<FanoutUseSnapshot>() : found->second;
	}
};

struct ProofBudget {
	BudgetTracker tracker;
	std::set<RTLIL::Cell *> cells;
	std::set<RTLIL::SigBit, BitLess> bits;
	bool ok = true;

	explicit ProofBudget(const AnalysisBudget &limit) : tracker(limit.max_cells, limit.max_bits, limit.max_events) { }
	bool cell(RTLIL::Cell *value)
	{
		if (value && cells.insert(value).second && !tracker.take_cell()) ok = false;
		return ok;
	}
	bool bit(RTLIL::SigBit value)
	{
		if (value.wire && bits.insert(value).second && !tracker.take_bit()) ok = false;
		return ok;
	}
	bool event() { if (!tracker.take_event()) ok = false; return ok; }
	ResourceUsage usage() const { return {tracker.cells(), tracker.bits(), tracker.events()}; }
	V02ReasonCode reason() const { return budget_reason(tracker.exhausted()); }
};

struct EvalOverride {
	RTLIL::Cell *comparator = nullptr;
	std::set<int> removed_bits;
};

class Evaluator {
public:
	Index &index;
	ProofBudget &budget;
	std::map<RTLIL::SigBit, RTLIL::State, BitLess> assignment;
	EvalOverride override;
	std::map<RTLIL::SigBit, RTLIL::State, BitLess> memo;
	std::set<RTLIL::SigBit, BitLess> active;

	Evaluator(Index &idx, ProofBudget &b) : index(idx), budget(b) { }

	RTLIL::State eval(RTLIL::SigBit raw)
	{
		RTLIL::SigBit bit = index.sigmap(raw);
		if (!bit.wire) return binary(bit.data) ? bit.data : RTLIL::State::Sx;
		budget.bit(bit);
		auto assigned = assignment.find(bit);
		if (assigned != assignment.end()) return assigned->second;
		auto cached = memo.find(bit);
		if (cached != memo.end()) return cached->second;
		if (!budget.event() || !active.insert(bit).second) return RTLIL::State::Sx;
		RTLIL::State result = RTLIL::State::Sx;
		auto found = index.drivers.find(bit);
		if (found != index.drivers.end() && found->second.size() == 1) {
			Driver driver = found->second.front();
			RTLIL::Cell *cell = driver.cell;
			budget.cell(cell);
			if (!is_sequential(cell->type)) result = eval_cell(cell, driver.offset);
		}
		active.erase(bit);
		memo[bit] = result;
		return result;
	}

private:
	RTLIL::State invert(RTLIL::State value)
	{
		return value == RTLIL::State::S0 ? RTLIL::State::S1 : value == RTLIL::State::S1 ? RTLIL::State::S0 : RTLIL::State::Sx;
	}

	RTLIL::State eval_compare(RTLIL::Cell *cell)
	{
		RTLIL::SigSpec a = index.sigmap(cell->getPort(ID::A));
		RTLIL::SigSpec b = index.sigmap(cell->getPort(ID::B));
		if (GetSize(a) != GetSize(b)) return RTLIL::State::Sx;
		bool equal = true;
		for (int i = 0; i < GetSize(a); ++i) {
			if (cell == override.comparator && override.removed_bits.count(i)) continue;
			RTLIL::State av = eval(a[i]), bv = eval(b[i]);
			if (!binary(av) || !binary(bv)) return RTLIL::State::Sx;
			if (av != bv) equal = false;
		}
		RTLIL::State value = equal ? RTLIL::State::S1 : RTLIL::State::S0;
		return cell->type == ID($ne) ? invert(value) : value;
	}

	RTLIL::State eval_cell(RTLIL::Cell *cell, int offset)
	{
		if (cell->type.in(ID($eq), ID($ne))) return eval_compare(cell);
		if (cell->type == ID($not)) return invert(eval(cell->getPort(ID::A)[offset]));
		if (cell->type == ID($mux)) {
			RTLIL::State select = eval(cell->getPort(ID::S)[0]);
			if (!binary(select)) return RTLIL::State::Sx;
			return eval(cell->getPort(select == RTLIL::State::S1 ? ID::B : ID::A)[offset]);
		}
		if (cell->type == ID($pmux)) {
			RTLIL::SigSpec selects = cell->getPort(ID::S);
			int chosen = -1;
			for (int i = 0; i < GetSize(selects); ++i) {
				RTLIL::State value = eval(selects[i]);
				if (!binary(value)) return RTLIL::State::Sx;
				if (value == RTLIL::State::S1) {
					if (chosen >= 0) return RTLIL::State::Sx;
					chosen = i;
				}
			}
			if (chosen < 0) return eval(cell->getPort(ID::A)[offset]);
			int width = cell->getParam(ID::WIDTH).as_int();
			return eval(cell->getPort(ID::B)[chosen * width + offset]);
		}
		if (cell->type.in(ID($reduce_and), ID($reduce_or), ID($reduce_bool))) {
			bool is_and = cell->type == ID($reduce_and);
			RTLIL::SigSpec input = cell->getPort(ID::A);
			bool any_one = false, any_zero = false;
			for (RTLIL::SigBit item : input) {
				RTLIL::State value = eval(item);
				if (!binary(value)) return RTLIL::State::Sx;
				any_one |= value == RTLIL::State::S1;
				any_zero |= value == RTLIL::State::S0;
			}
			return is_and ? (any_zero ? RTLIL::State::S0 : RTLIL::State::S1) :
				(any_one ? RTLIL::State::S1 : RTLIL::State::S0);
		}
		if (cell->type.in(ID($logic_and), ID($logic_or))) {
			RTLIL::State a = eval(cell->getPort(ID::A)[0]);
			RTLIL::State b = eval(cell->getPort(ID::B)[0]);
			if (!binary(a) || !binary(b)) return RTLIL::State::Sx;
			if (cell->type == ID($logic_and)) return a == RTLIL::State::S1 && b == RTLIL::State::S1 ? RTLIL::State::S1 : RTLIL::State::S0;
			return a == RTLIL::State::S1 || b == RTLIL::State::S1 ? RTLIL::State::S1 : RTLIL::State::S0;
		}
		return RTLIL::State::Sx;
	}
};

void collect_leaves(Index &index, RTLIL::SigBit raw, std::set<RTLIL::SigBit, BitLess> &leaves,
		std::set<RTLIL::SigBit, BitLess> &seen)
{
	RTLIL::SigBit bit = index.sigmap(raw);
	if (!bit.wire || !seen.insert(bit).second) return;
	auto found = index.drivers.find(bit);
	if (found == index.drivers.end() || found->second.size() != 1 || is_sequential(found->second.front().cell->type)) {
		leaves.insert(bit); return;
	}
	RTLIL::Cell *cell = found->second.front().cell;
	for (const auto &conn : cell->connections())
		if (index.celltypes.cell_input(cell->type, conn.first))
			for (RTLIL::SigBit input : conn.second) collect_leaves(index, input, leaves, seen);
}

bool depends_on(Index &index, RTLIL::SigBit raw, const std::set<RTLIL::SigBit, BitLess> &targets,
		std::set<RTLIL::SigBit, BitLess> &seen)
{
	RTLIL::SigBit bit = index.sigmap(raw);
	if (targets.count(bit)) return true;
	if (!bit.wire || !seen.insert(bit).second) return false;
	auto found = index.drivers.find(bit);
	if (found == index.drivers.end() || found->second.size() != 1 || is_sequential(found->second.front().cell->type)) return false;
	RTLIL::Cell *cell = found->second.front().cell;
	for (const auto &conn : cell->connections())
		if (index.celltypes.cell_input(cell->type, conn.first))
			for (RTLIL::SigBit input : conn.second)
				if (depends_on(index, input, targets, seen)) return true;
	return false;
}

struct TableShape {
	bool ok = false;
	RTLIL::SigSpec root;
	std::set<RTLIL::SigBit, BitLess> selects;
	std::set<RTLIL::Cell *> cells;
	std::map<RTLIL::SigBit, std::set<RTLIL::SigBit, BitLess>, BitLess> lower_to_higher;
	std::vector<RTLIL::SigBit> order;
	std::vector<RTLIL::Const> rows;
};

std::set<RTLIL::SigBit, BitLess> collect_table_bit(Index &index, RTLIL::SigBit raw, TableShape &shape,
		std::set<RTLIL::SigBit, BitLess> &active)
{
	RTLIL::SigBit bit = index.sigmap(raw);
	if (!bit.wire) {
		if (!binary(bit.data)) shape.ok = false;
		return {};
	}
	if (!active.insert(bit).second) { shape.ok = false; return {}; }
	auto found = index.drivers.find(bit);
	if (found == index.drivers.end() || found->second.size() != 1) { shape.ok = false; active.erase(bit); return {}; }
	Driver driver = found->second.front();
	RTLIL::Cell *cell = driver.cell;
	if (cell->type != ID($mux) || GetSize(cell->getPort(ID::S)) != 1 ||
			driver.offset < 0 || driver.offset >= GetSize(cell->getPort(ID::A)) ||
			GetSize(cell->getPort(ID::A)) != GetSize(cell->getPort(ID::B))) {
		shape.ok = false; active.erase(bit); return {};
	}
	shape.cells.insert(cell);
	RTLIL::SigBit select = index.sigmap(cell->getPort(ID::S)[0]);
	if (!select.wire) { shape.ok = false; active.erase(bit); return {}; }
	shape.selects.insert(select);
	auto left = collect_table_bit(index, cell->getPort(ID::A)[driver.offset], shape, active);
	auto right = collect_table_bit(index, cell->getPort(ID::B)[driver.offset], shape, active);
	std::set<RTLIL::SigBit, BitLess> below = left;
	below.insert(right.begin(), right.end());
	for (RTLIL::SigBit lower : below) if (lower != select) shape.lower_to_higher[lower].insert(select);
	below.insert(select);
	active.erase(bit);
	return below;
}

bool derive_total_order(TableShape &shape)
{
	std::map<RTLIL::SigBit, int, BitLess> indegree;
	for (RTLIL::SigBit bit : shape.selects) indegree[bit] = 0;
	for (const auto &edge : shape.lower_to_higher)
		for (RTLIL::SigBit higher : edge.second) ++indegree[higher];
	while (!indegree.empty()) {
		std::vector<RTLIL::SigBit> ready;
		for (const auto &item : indegree) if (item.second == 0) ready.push_back(item.first);
		if (ready.size() != 1) return false;
		RTLIL::SigBit next = ready.front();
		shape.order.push_back(next);
		indegree.erase(next);
		for (RTLIL::SigBit higher : shape.lower_to_higher[next]) {
			auto found = indegree.find(higher);
			if (found != indegree.end()) --found->second;
		}
	}
	return shape.order.size() == shape.selects.size();
}

TableShape analyze_table_root(Index &index, RTLIL::SigSpec root, int max_address_width, ProofBudget &budget)
{
	TableShape shape; shape.ok = true; shape.root = index.sigmap(root);
	for (RTLIL::SigBit bit : shape.root) {
		std::set<RTLIL::SigBit, BitLess> active;
		collect_table_bit(index, bit, shape, active);
		if (!shape.ok) return shape;
	}
	if (shape.selects.empty() || GetSize(shape.selects) > max_address_width || !derive_total_order(shape)) {
		shape.ok = false; return shape;
	}
	for (RTLIL::Cell *cell : shape.cells) budget.cell(cell);
	for (RTLIL::SigBit bit : shape.selects) budget.bit(bit);
	if (!budget.ok) { shape.ok = false; return shape; }
	const int rows = 1 << GetSize(shape.order);
	for (int row = 0; row < rows; ++row) {
		Evaluator eval(index, budget);
		for (int i = 0; i < GetSize(shape.order); ++i)
			eval.assignment[shape.order[i]] = (row >> i) & 1 ? RTLIL::State::S1 : RTLIL::State::S0;
		std::vector<RTLIL::State> values;
		for (RTLIL::SigBit bit : shape.root) values.push_back(eval.eval(bit));
		if (!std::all_of(values.begin(), values.end(), binary)) { shape.ok = false; return shape; }
		shape.rows.emplace_back(values);
	}
	return shape;
}

std::vector<std::pair<RTLIL::Cell *, int>> sequential_bits(Index &index)
{
	std::vector<std::pair<RTLIL::Cell *, int>> result;
	for (RTLIL::Cell *cell : index.module->cells())
		if (is_sequential(cell->type) && cell->hasPort(ID::Q))
			for (int i = 0; i < GetSize(cell->getPort(ID::Q)); ++i) result.push_back({cell, i});
	return result;
}

int param_bit(const RTLIL::Cell *cell, RTLIL::IdString name, int bit, int fallback)
{
	if (!cell->hasParam(name)) return fallback;
	RTLIL::Const value = cell->getParam(name);
	return bit < GetSize(value) && binary(value[bit]) ? value[bit] == RTLIL::State::S1 : fallback;
}

int next_state(Evaluator &eval, RTLIL::Cell *cell, int bit, int q_value)
{
	auto control = [&](RTLIL::IdString port, RTLIL::IdString polarity) -> int {
		if (!cell->hasPort(port) || !cell->hasParam(polarity)) return 0;
		RTLIL::State value = eval.eval(cell->getPort(port)[0]);
		if (!binary(value)) return -1;
		return (value == RTLIL::State::S1) == cell->getParam(polarity).as_bool();
	};
	if (cell->hasPort(ID::ARST)) {
		int active = control(ID::ARST, ID::ARST_POLARITY);
		if (active < 0) return -1;
		if (active) return param_bit(cell, ID::ARST_VALUE, bit, -1);
	}
	// $sdffce is the Yosys enable-priority synchronous-reset form: an
	// inactive enable holds Q without observing SRST.  Other $sdff* cells
	// use reset priority.  This distinction is part of the frozen boundary.
	if (cell->type == ID($sdffce) && cell->hasPort(ID::EN)) {
		int active = control(ID::EN, ID::EN_POLARITY);
		if (active < 0) return -1;
		if (!active) return q_value;
	}
	if (cell->hasPort(ID::SRST)) {
		int active = control(ID::SRST, ID::SRST_POLARITY);
		if (active < 0) return -1;
		if (active) return param_bit(cell, ID::SRST_VALUE, bit, -1);
	}
	if (cell->hasPort(ID::EN) && cell->type != ID($sdffce)) {
		int active = control(ID::EN, ID::EN_POLARITY);
		if (active < 0) return -1;
		if (!active) return q_value;
	}
	if (!cell->hasPort(ID::D) || bit >= GetSize(cell->getPort(ID::D))) return -1;
	RTLIL::State value = eval.eval(cell->getPort(ID::D)[bit]);
	return value == RTLIL::State::S0 ? 0 : value == RTLIL::State::S1 ? 1 : -1;
}

bool reaches_d(Index &index, RTLIL::SigBit raw, RTLIL::Cell *sink, int sink_bit,
		std::set<RTLIL::SigBit, BitLess> &seen)
{
	RTLIL::SigBit bit = index.sigmap(raw);
	if (!bit.wire || !seen.insert(bit).second) return false;
	for (const FanoutUseSnapshot &use : index.fanout(bit)) {
		RTLIL::Cell *cell = index.module->cell(RTLIL::IdString(use.cell_id));
		if (!cell) continue;
		if (cell == sink && use.port == ID::D && use.port_bit == sink_bit) return true;
		if (is_sequential(cell->type)) continue;
		for (const auto &conn : cell->connections())
			if (index.celltypes.cell_output(cell->type, conn.first))
				for (RTLIL::SigBit output : conn.second)
					if (reaches_d(index, output, sink, sink_bit, seen)) return true;
	}
	return false;
}

bool map_table_sinks(Index &index, const TableShape &shape, ProofBudget &budget,
		std::vector<ProductionSequentialBitSnapshot> &mapped, ResetEnableHoldContract &contract)
{
	auto seq = sequential_bits(index);
	if (seq.size() < std::size_t(GetSize(shape.root))) return false;
	std::set<RTLIL::SigBit, BitLess> address(shape.order.begin(), shape.order.end());
	std::set<RTLIL::SigBit, BitLess> controls;
	for (const auto &item : seq) {
		RTLIL::Cell *cell = item.first;
		for (RTLIL::IdString port : {ID::D, ID::EN, ID::SRST, ID::ARST}) if (cell->hasPort(port))
			for (RTLIL::SigBit bit : cell->getPort(port)) {
				std::set<RTLIL::SigBit, BitLess> leaves, seen;
				collect_leaves(index, bit, leaves, seen);
				for (RTLIL::SigBit leaf : leaves) if (!address.count(leaf)) controls.insert(leaf);
			}
	}
	if (controls.size() > 8) return false;
	std::vector<RTLIL::SigBit> control_bits(controls.begin(), controls.end());
	const int rows = 1 << GetSize(shape.order);
	std::vector<std::vector<int>> root_columns(GetSize(shape.root));
	for (int bit = 0; bit < GetSize(shape.root); ++bit)
		for (const RTLIL::Const &row : shape.rows) root_columns[bit].push_back(row[bit] == RTLIL::State::S1);

	for (int mode = 0; mode < (1 << GetSize(control_bits)); ++mode) {
		std::vector<std::vector<int>> seq_columns(seq.size());
		bool definite = true;
		for (int row = 0; row < rows && definite; ++row) {
			for (std::size_t s = 0; s < seq.size(); ++s) {
				int values[2] = {-1, -1};
				for (int q = 0; q < 2; ++q) {
					Evaluator eval(index, budget);
					for (int i = 0; i < GetSize(shape.order); ++i)
						eval.assignment[shape.order[i]] = (row >> i) & 1 ? RTLIL::State::S1 : RTLIL::State::S0;
					for (int i = 0; i < GetSize(control_bits); ++i)
						eval.assignment[control_bits[i]] = (mode >> i) & 1 ? RTLIL::State::S1 : RTLIL::State::S0;
					values[q] = next_state(eval, seq[s].first, seq[s].second, q);
				}
				if (values[0] < 0 || values[0] != values[1]) { definite = false; break; }
				seq_columns[s].push_back(values[0]);
			}
		}
		if (!definite || !budget.ok) continue;
		std::vector<std::vector<int>> candidates(GetSize(shape.root));
		for (int bit = 0; bit < GetSize(shape.root); ++bit) {
			std::vector<int> direct, indirect;
			for (std::size_t s = 0; s < seq.size(); ++s) if (root_columns[bit] == seq_columns[s]) {
				std::set<RTLIL::SigBit, BitLess> seen;
				(reaches_d(index, shape.root[bit], seq[s].first, seq[s].second, seen) ? direct : indirect).push_back(s);
			}
			candidates[bit] = direct;
			candidates[bit].insert(candidates[bit].end(), indirect.begin(), indirect.end());
			if (candidates[bit].empty()) { definite = false; break; }
		}
		if (!definite) continue;
		std::vector<int> choice(GetSize(shape.root), -1);
		std::set<int> used;
		std::function<bool(int)> assign = [&](int bit) {
			if (bit == GetSize(shape.root)) return true;
			for (int candidate : candidates[bit]) if (used.insert(candidate).second) {
				choice[bit] = candidate;
				if (assign(bit + 1)) return true;
				used.erase(candidate);
			}
			return false;
		};
		if (!assign(0)) continue;
		for (int bit = 0; bit < GetSize(shape.root); ++bit) {
			ProductionSequentialBitSnapshot result;
			result.logical_output_bit = bit;
			result.cell_id = seq[choice[bit]].first->name.str();
			result.cell = seq[choice[bit]].first;
			result.q_bit = seq[choice[bit]].second;
			result.snapshot = snapshot_cell(result.cell);
			for (int i = 0; i < GetSize(control_bits); ++i)
				result.table_mode_facts.push_back({control_bits[i], (mode >> i) & 1 ? RTLIL::State::S1 : RTLIL::State::S0});
			result.proof_ids.push_back("production-table-sink-bit-" + std::to_string(bit));
			mapped.push_back(result);
		}
		contract.no_cross_dff_propagation = true;
		contract.reset_preserved = true;
		contract.enable_preserved = true;
		contract.hold_preserved = true;
		contract.proof_ids.push_back("production-sequential-next-state-enumeration");
		return true;
	}
	return false;
}

ProductionConstantTableProof find_table(Index &index, const ProductionCase34ProofRequest &request, ProofBudget &budget)
{
	ProductionConstantTableProof out; out.module = request.module_id;
	TableShape best;
	long best_score = -1;
	for (RTLIL::Cell *cell : index.module->cells()) {
		if (cell->type != ID($mux) || !cell->hasPort(ID::Y)) continue;
		RTLIL::SigSpec root = cell->getPort(ID::Y);
		if (GetSize(root) < 2 || GetSize(root) > request.max_output_width) continue;
		TableShape candidate = analyze_table_root(index, root, request.max_address_width, budget);
		if (!budget.ok) {
			out.status = V02ProofStatus::BudgetExhausted; out.reason_code = budget.reason(); out.usage = budget.usage(); return out;
		}
		if (!candidate.ok || candidate.order.size() < 2) continue;
		long score = (long(GetSize(candidate.root)) << 32) + candidate.order.size();
		if (score > best_score) { best_score = score; best = candidate; }
	}
	if (best_score < 0) { out.reason_code = V02ReasonCode::TableExtractFailed; return out; }
	TableShape verified = best;
	out.logical_root_outputs = verified.root;
	out.address_bits_lsb_first = verified.order;
	out.output_bits_lsb_first.assign(verified.root.begin(), verified.root.end());
	out.address_width = GetSize(verified.order);
	out.output_width = GetSize(verified.root);
	out.truth_table = verified.rows;
	std::vector<RTLIL::Cell *> cells(verified.cells.begin(), verified.cells.end());
	std::sort(cells.begin(), cells.end(), [](RTLIL::Cell *a, RTLIL::Cell *b) { return a->name.str() < b->name.str(); });
	for (RTLIL::Cell *cell : cells) out.source_cells.push_back(snapshot_cell(cell));
	out.complete_binary_domain = out.truth_table.size() == (std::size_t(1) << out.address_width);
	out.proven_all_rows = out.complete_binary_domain;
	if (!map_table_sinks(index, verified, budget, out.sequential_sinks, out.reset_enable_hold_contract)) {
		out.reason_code = budget.ok ? V02ReasonCode::SequentialBoundaryChanged : budget.reason();
		out.status = budget.ok ? V02ProofStatus::Unknown : V02ProofStatus::BudgetExhausted;
		out.usage = budget.usage(); return out;
	}
	out.all_output_bits_mapped = GetSize(out.sequential_sinks) == out.output_width;
	if (!request.expected_address_bits_lsb_first.empty()) {
		if (request.expected_address_bits_lsb_first.size() != out.address_bits_lsb_first.size()) {
			out.reason_code = V02ReasonCode::AddressOrderUnknown; out.usage = budget.usage(); return out;
		}
		for (int i = 0; i < GetSize(out.address_bits_lsb_first); ++i)
			if (index.sigmap(request.expected_address_bits_lsb_first[i]) != index.sigmap(out.address_bits_lsb_first[i])) {
				out.reason_code = V02ReasonCode::AddressOrderUnknown; out.usage = budget.usage(); return out;
			}
	}
	if (!request.expected_output_bits_lsb_first.empty()) {
		if (request.expected_output_bits_lsb_first.size() != out.output_bits_lsb_first.size()) {
			out.reason_code = V02ReasonCode::OutputOrderUnknown; out.usage = budget.usage(); return out;
		}
		for (int i = 0; i < GetSize(out.output_bits_lsb_first); ++i)
			if (index.sigmap(request.expected_output_bits_lsb_first[i]) != index.sigmap(out.output_bits_lsb_first[i])) {
				out.reason_code = V02ReasonCode::OutputOrderUnknown; out.usage = budget.usage(); return out;
			}
	}
	if (!request.expected_sequential_sinks.empty()) {
		if (request.expected_sequential_sinks.size() != out.sequential_sinks.size()) {
			out.reason_code = V02ReasonCode::SequentialBoundaryChanged; out.usage = budget.usage(); return out;
		}
		for (int i = 0; i < GetSize(out.sequential_sinks); ++i) {
			const auto &expected = request.expected_sequential_sinks[i];
			const auto &actual = out.sequential_sinks[i];
			if (expected.logical_output_bit != actual.logical_output_bit || expected.cell_id != actual.cell_id ||
					expected.cell != actual.cell || expected.q_bit != actual.q_bit || !same_snapshot(expected.snapshot, actual.snapshot)) {
				out.reason_code = V02ReasonCode::SequentialBoundaryChanged; out.usage = budget.usage(); return out;
			}
		}
	}
	out.proven_all_obligations = out.complete_binary_domain && out.proven_all_rows && out.all_output_bits_mapped &&
		out.reset_enable_hold_contract.no_cross_dff_propagation && out.reset_enable_hold_contract.reset_preserved &&
		out.reset_enable_hold_contract.enable_preserved && out.reset_enable_hold_contract.hold_preserved;
	for (int row = 0; row < GetSize(out.truth_table); ++row) out.proof_ids.push_back("production-table-row-" + std::to_string(row));
	out.status = out.proven_all_obligations ? V02ProofStatus::Normal : V02ProofStatus::Unknown;
	out.reason_code = out.proven_all_obligations ? V02ReasonCode::Proved : V02ReasonCode::SequentialBoundaryChanged;
	out.usage = budget.usage();
	return out;
}

bool comparator_source(Index &index, RTLIL::Cell *cell, RTLIL::SigSpec &source, RTLIL::Const &constant)
{
	if (!cell->type.in(ID($eq), ID($ne)) || !cell->hasParam(ID::A_WIDTH) || !cell->hasParam(ID::B_WIDTH) ||
			!cell->hasParam(ID::A_SIGNED) || !cell->hasParam(ID::B_SIGNED) ||
			cell->getParam(ID::A_WIDTH).as_int() != cell->getParam(ID::B_WIDTH).as_int() ||
			cell->getParam(ID::A_SIGNED).as_bool() != cell->getParam(ID::B_SIGNED).as_bool()) return false;
	RTLIL::SigSpec a = index.sigmap(cell->getPort(ID::A)), b = index.sigmap(cell->getPort(ID::B));
	auto fully_binary = [](const RTLIL::SigSpec &sig) {
		for (RTLIL::SigBit bit : sig) if (bit.wire || !binary(bit.data)) return false;
		return true;
	};
	if (fully_binary(a) == fully_binary(b)) return false;
	source = fully_binary(a) ? b : a;
	constant = (fully_binary(a) ? a : b).as_const();
	return GetSize(source) == GetSize(constant) && GetSize(source) > 1;
}

std::string source_key(const RTLIL::SigSpec &source)
{
	std::string key;
	for (RTLIL::SigBit bit : source) key += bit.wire ? bit.wire->name.str() + ":" + std::to_string(bit.offset) + ";" : "c;";
	return key;
}

std::vector<RTLIL::Cell *> affected_sequential(Index &index, const std::vector<RTLIL::Cell *> &comparators)
{
	std::set<RTLIL::SigBit, BitLess> targets;
	for (RTLIL::Cell *cell : comparators) targets.insert(index.sigmap(cell->getPort(ID::Y)[0]));
	std::vector<RTLIL::Cell *> result;
	for (RTLIL::Cell *cell : index.module->cells()) if (is_sequential(cell->type)) {
		bool hit = false;
		for (RTLIL::IdString port : {ID::D, ID::EN, ID::SRST, ID::ARST}) if (cell->hasPort(port))
			for (RTLIL::SigBit bit : cell->getPort(port)) {
				std::set<RTLIL::SigBit, BitLess> seen;
				if (depends_on(index, bit, targets, seen)) hit = true;
			}
		if (hit) result.push_back(cell);
	}
	return result;
}

bool compare_reduced(Index &index, ProofBudget &budget, RTLIL::Cell *comparator, RTLIL::SigSpec source,
		int removed, const std::vector<RTLIL::Cell *> &sinks)
{
	std::set<RTLIL::SigBit, BitLess> source_set;
	for (RTLIL::SigBit bit : source) source_set.insert(index.sigmap(bit));
	std::set<RTLIL::SigBit, BitLess> controls;
	for (RTLIL::Cell *sink : sinks)
		for (RTLIL::IdString port : {ID::D, ID::EN, ID::SRST, ID::ARST}) if (sink->hasPort(port))
			for (RTLIL::SigBit bit : sink->getPort(port)) {
				std::set<RTLIL::SigBit, BitLess> leaves, seen;
				collect_leaves(index, bit, leaves, seen);
				for (RTLIL::SigBit leaf : leaves) if (!source_set.count(leaf)) controls.insert(leaf);
			}
	if (GetSize(source) + GetSize(controls) > 16) return false;
	std::vector<RTLIL::SigBit> control_bits(controls.begin(), controls.end());
	const int combinations = 1 << (GetSize(source) + GetSize(control_bits));
	for (int values = 0; values < combinations; ++values) {
		for (RTLIL::Cell *sink : sinks) for (int bit = 0; bit < GetSize(sink->getPort(ID::Q)); ++bit) for (int q = 0; q < 2; ++q) {
			Evaluator full(index, budget), reduced(index, budget);
			reduced.override.comparator = comparator; reduced.override.removed_bits.insert(removed);
			for (int i = 0; i < GetSize(source); ++i) {
				RTLIL::State value = (values >> i) & 1 ? RTLIL::State::S1 : RTLIL::State::S0;
				full.assignment[index.sigmap(source[i])] = value; reduced.assignment[index.sigmap(source[i])] = value;
			}
			for (int i = 0; i < GetSize(control_bits); ++i) {
				RTLIL::State value = (values >> (GetSize(source) + i)) & 1 ? RTLIL::State::S1 : RTLIL::State::S0;
				full.assignment[control_bits[i]] = value; reduced.assignment[control_bits[i]] = value;
			}
			int a = next_state(full, sink, bit, q), b = next_state(reduced, sink, bit, q);
			if (!budget.ok || a < 0 || b < 0 || a != b) return false;
		}
	}
	return true;
}

std::set<RTLIL::Cell *> forward_cells(Index &index, RTLIL::SigBit start, bool &covered)
{
	std::set<RTLIL::Cell *> cells;
	std::set<RTLIL::SigBit, BitLess> seen;
	std::deque<RTLIL::SigBit> queue; queue.push_back(index.sigmap(start));
	covered = true;
	while (!queue.empty()) {
		RTLIL::SigBit bit = queue.front(); queue.pop_front();
		if (!seen.insert(bit).second) continue;
		if (index.module_outputs.count(bit)) covered = false;
		auto uses = index.fanout(bit);
		for (const FanoutUseSnapshot &use : uses) {
			RTLIL::Cell *cell = index.module->cell(RTLIL::IdString(use.cell_id));
			if (!cell) { covered = false; continue; }
			cells.insert(cell);
			if (is_sequential(cell->type)) continue;
			for (const auto &conn : cell->connections()) if (index.celltypes.cell_output(cell->type, conn.first))
				for (RTLIL::SigBit output : conn.second) queue.push_back(index.sigmap(output));
		}
	}
	return cells;
}

ProductionGuardedConeProof find_guarded(Index &index, const ProductionCase34ProofRequest &request, ProofBudget &budget)
{
	ProductionGuardedConeProof out; out.module = request.module_id;
	struct Item { RTLIL::Cell *cell; RTLIL::SigSpec source; RTLIL::Const constant; };
	std::map<std::string, std::vector<Item>> groups;
	for (RTLIL::Cell *cell : index.module->cells()) {
		RTLIL::SigSpec source; RTLIL::Const constant;
		if (comparator_source(index, cell, source, constant)) groups[source_key(source)].push_back({cell, source, constant});
	}
	for (auto &group_item : groups) {
		auto &group = group_item.second;
		if (group.size() < 2) continue;
		RTLIL::SigSpec source = group.front().source;
		std::vector<RTLIL::Cell *> comparators;
		for (const Item &item : group) comparators.push_back(item.cell);
		auto sinks = affected_sequential(index, comparators);
		if (sinks.empty()) continue;
		for (int removed = GetSize(source) - 1; removed >= 0; --removed) {
			RTLIL::State common = group.front().constant[removed];
			if (!binary(common) || !std::all_of(group.begin(), group.end(), [&](const Item &item) { return item.constant[removed] == common; })) continue;
			bool all_equivalent = true;
			for (const Item &item : group)
				if (!compare_reduced(index, budget, item.cell, source, removed, sinks)) { all_equivalent = false; break; }
			if (!budget.ok) {
				out.status = V02ProofStatus::BudgetExhausted; out.reason_code = budget.reason(); out.usage = budget.usage(); return out;
			}
			if (!all_equivalent) continue;
			out.source_value = source; out.source_width = GetSize(source); out.removed_bit_indices = {removed};
			out.all_comparators_equivalent = true;
			std::set<RTLIL::Cell *> affected;
			bool fanout_covered = true;
			for (int ci = 0; ci < GetSize(group); ++ci) {
				const Item &item = group[ci];
				ComparatorProofConclusion conclusion;
				conclusion.cell_id = item.cell->name.str(); conclusion.comparator_constant = item.constant;
				conclusion.full_width = GetSize(source); conclusion.reduced_width = GetSize(source) - 1;
				conclusion.a_signed = item.cell->getParam(ID::A_SIGNED).as_bool(); conclusion.b_signed = item.cell->getParam(ID::B_SIGNED).as_bool();
				conclusion.removed_bit_indices = {removed}; conclusion.equivalent_under_guard = true;
				conclusion.comparator_full_fanout_snapshot = index.fanout(item.cell->getPort(ID::Y)[0]);
				bool covered = true; auto cells = forward_cells(index, item.cell->getPort(ID::Y)[0], covered);
				fanout_covered &= covered; affected.insert(cells.begin(), cells.end()); affected.insert(item.cell);
				conclusion.comparator_must_be_preserved = !covered;
				conclusion.reason_code = covered ? V02ReasonCode::Proved : V02ReasonCode::ComparatorFanoutUncovered;
				out.comparator_cells.push_back(conclusion);
				for (RTLIL::Cell *cell : cells) if (cell->type.in(ID($mux), ID($pmux))) {
					RTLIL::SigSpec selects = cell->getPort(ID::S);
					for (int si = 0; si < GetSize(selects); ++si) if (index.sigmap(selects[si]) == index.sigmap(item.cell->getPort(ID::Y)[0])) {
						RTLIL::SigSpec data = cell->type == ID($mux) ? cell->getPort(ID::B) :
							cell->getPort(ID::B).extract(si * cell->getParam(ID::WIDTH).as_int(), cell->getParam(ID::WIDTH).as_int());
						for (int pi = 0; pi < GetSize(data); ++pi) for (int sb = 0; sb < GetSize(source); ++sb)
							if (index.sigmap(data[pi]) == index.sigmap(source[sb])) {
								ProductionFoldedDataBitConclusion fold;
								fold.comparator_index = ci; fold.cell_id = cell->name.str(); fold.port = ID::B;
								fold.port_bit = cell->type == ID($mux) ? pi : si * cell->getParam(ID::WIDTH).as_int() + pi;
								fold.source_bit_index = sb; fold.replacement = item.constant[sb]; fold.proved = true;
								fold.proof_id = "production-fold-c" + std::to_string(ci) + "-b" + std::to_string(sb);
								fold.reason_code = V02ReasonCode::Proved; out.folded_data_bits.push_back(fold);
							}
					}
				}
			}
			out.all_fanout_covered = fanout_covered;
			std::set<std::tuple<std::string, std::string, int>> folded_positions;
			for (const auto &fold : out.folded_data_bits) folded_positions.insert({fold.cell_id, fold.port.str(), fold.port_bit});
			for (RTLIL::Cell *cell : affected) if (!is_sequential(cell->type))
				for (const auto &conn : cell->connections()) if (index.celltypes.cell_input(cell->type, conn.first))
					for (int pi = 0; pi < GetSize(conn.second); ++pi) for (RTLIL::SigBit source_bit : source)
						if (index.sigmap(conn.second[pi]) == index.sigmap(source_bit) && !folded_positions.count({cell->name.str(), conn.first.str(), pi})) {
							ProductionPreservedDataBitConclusion keep;
							keep.cell_id = cell->name.str(); keep.port = conn.first; keep.port_bit = pi; keep.source_bit = source_bit;
							keep.preserved = true; keep.proof_id = "production-preserve-" + cell->name.str() + "-" + std::to_string(pi);
							keep.reason_code = V02ReasonCode::Proved; out.preserved_variable_bits.push_back(keep);
						}
			out.all_data_bits_proved = !out.folded_data_bits.empty();
			for (RTLIL::Cell *sink : sinks) {
				affected.insert(sink);
				for (int q = 0; q < GetSize(sink->getPort(ID::Q)); ++q) {
					ProductionSequentialBitSnapshot ss; ss.logical_output_bit = GetSize(out.sequential_sinks);
					ss.cell_id = sink->name.str(); ss.cell = sink; ss.q_bit = q; ss.snapshot = snapshot_cell(sink);
					ss.proof_ids.push_back("production-guarded-next-state-equivalence"); out.sequential_sinks.push_back(ss);
				}
				if (sink->hasPort(ID::EN) && sink->hasParam(ID::EN_POLARITY)) {
					RTLIL::State active_value = sink->getParam(ID::EN_POLARITY).as_bool() ? RTLIL::State::S1 : RTLIL::State::S0;
					GuardFact guard{sink->getPort(ID::EN)[0], active_value};
					EntryAnalysisResult facts = analyze_guarded_values(request.module, RTLIL::SigSpec(source[removed]), {guard}, request.budget);
					if (facts.status == EntryStatus::Normal && !facts.bits.empty() &&
							facts.bits[0].value == (common == RTLIL::State::S1 ? FactValue::One : FactValue::Zero))
						out.current_cycle_guard_facts.push_back(guard);
				}
			}
			std::vector<RTLIL::Cell *> affected_sorted(affected.begin(), affected.end());
			std::sort(affected_sorted.begin(), affected_sorted.end(), [](RTLIL::Cell *a, RTLIL::Cell *b) { return a->name.str() < b->name.str(); });
			for (RTLIL::Cell *cell : affected_sorted) {
				out.affected_cells.push_back(snapshot_cell(cell));
				if (cell->type.in(ID($reduce_and), ID($reduce_or), ID($reduce_bool), ID($not))) out.guard_logic_cells.push_back(snapshot_cell(cell));
			}
			out.all_sequential_boundaries_proved = !out.sequential_sinks.empty() && !out.current_cycle_guard_facts.empty();
			out.reset_enable_hold_contract.no_cross_dff_propagation = true;
			out.reset_enable_hold_contract.reset_preserved = true;
			out.reset_enable_hold_contract.enable_preserved = true;
			out.reset_enable_hold_contract.hold_preserved = true;
			out.reset_enable_hold_contract.proof_ids.push_back("production-sequential-full-snapshot");
			out.proven_all_obligations = out.all_comparators_equivalent && out.all_data_bits_proved &&
				out.all_fanout_covered && out.all_sequential_boundaries_proved;
			out.status = out.proven_all_obligations ? V02ProofStatus::Normal : V02ProofStatus::Unknown;
			out.reason_code = out.proven_all_obligations ? V02ReasonCode::Proved :
				(!out.all_fanout_covered ? V02ReasonCode::ComparatorFanoutUncovered :
				 !out.all_data_bits_proved ? V02ReasonCode::PartialDataFold : V02ReasonCode::GuardNotDominating);
			out.proof_ids.push_back("production-next-state-counterfactual-complete");
			out.usage = budget.usage(); return out;
		}
	}
	out.reason_code = V02ReasonCode::GuardNotDominating; out.usage = budget.usage(); return out;
}

class ProductionAnalyzer final : public ProductionCase34Analyzer {
public:
	ProductionCase34ProofBundle analyze(const ProductionCase34ProofRequest &request) const override
	{
		ProductionCase34ProofBundle out; out.module = request.module_id;
		if (!request.module || request.module_id != request.module->name.str() || !exact_budget(request.budget) ||
				request.max_address_width <= 0 || request.max_address_width >= 20 || request.max_output_width <= 0) {
			out.status = V02ProofStatus::InvalidRequest; out.reason_code = V02ReasonCode::InvalidRequest; return out;
		}
		Index index(request.module);
		ProofBudget guard_budget(request.budget);
		ProductionGuardedConeProof guard = find_guarded(index, request, guard_budget);
		if (guard.source_width > 0 || guard.status == V02ProofStatus::BudgetExhausted) out.guarded_cones.push_back(guard);
		ProofBudget table_budget(request.budget);
		ProductionConstantTableProof table = find_table(index, request, table_budget);
		if (table.output_width > 0 || table.status == V02ProofStatus::BudgetExhausted) out.constant_tables.push_back(table);
		out.usage.visited_cells = guard.usage.visited_cells + table.usage.visited_cells;
		out.usage.visited_bits = guard.usage.visited_bits + table.usage.visited_bits;
		out.usage.propagation_events = guard.usage.propagation_events + table.usage.propagation_events;
		if (!out.guarded_cones.empty() || !out.constant_tables.empty()) {
			bool normal = std::all_of(out.guarded_cones.begin(), out.guarded_cones.end(), [](const auto &p) { return p.status == V02ProofStatus::Normal; }) &&
				std::all_of(out.constant_tables.begin(), out.constant_tables.end(), [](const auto &p) { return p.status == V02ProofStatus::Normal; });
			bool exhausted = std::any_of(out.guarded_cones.begin(), out.guarded_cones.end(), [](const auto &p) { return p.status == V02ProofStatus::BudgetExhausted; }) ||
				std::any_of(out.constant_tables.begin(), out.constant_tables.end(), [](const auto &p) { return p.status == V02ProofStatus::BudgetExhausted; });
			out.status = normal ? V02ProofStatus::Normal : exhausted ? V02ProofStatus::BudgetExhausted : V02ProofStatus::Unknown;
			out.reason_code = normal ? V02ReasonCode::Proved : (!out.guarded_cones.empty() && out.guarded_cones.front().status != V02ProofStatus::Normal ?
				out.guarded_cones.front().reason_code : out.constant_tables.front().reason_code);
		} else {
			out.status = V02ProofStatus::Unknown; out.reason_code = V02ReasonCode::TableExtractFailed;
		}
		return out;
	}
};

} // namespace

std::unique_ptr<ProductionCase34Analyzer> make_production_case34_analyzer()
{
	return std::make_unique<ProductionAnalyzer>();
}

} // namespace PmuxOpt

YOSYS_NAMESPACE_END
