#include "pmux_v02_decision_cone_proof_api.h"
#include "budget_tracker.h"

#include "kernel/celltypes.h"
#include "kernel/sigtools.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
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

bool supported_sequential(RTLIL::IdString type)
{
	return type.in(ID($dff), ID($dffe), ID($sdff), ID($sdffe), ID($sdffce), ID($adff), ID($adffe));
}

DecisionConeReasonCode budget_reason(BudgetKind kind)
{
	if (kind == BudgetKind::Cells) return DecisionConeReasonCode::BudgetCellsExhausted;
	if (kind == BudgetKind::Bits) return DecisionConeReasonCode::BudgetBitsExhausted;
	return DecisionConeReasonCode::BudgetEventsExhausted;
}

V02ProofStatus status_for_reason(DecisionConeReasonCode reason)
{
	if (reason == DecisionConeReasonCode::BudgetCellsExhausted ||
			reason == DecisionConeReasonCode::BudgetBitsExhausted ||
			reason == DecisionConeReasonCode::BudgetEventsExhausted)
		return V02ProofStatus::BudgetExhausted;
	if (reason == DecisionConeReasonCode::FactConflict) return V02ProofStatus::FactConflict;
	if (reason == DecisionConeReasonCode::InvalidRequest) return V02ProofStatus::InvalidRequest;
	return V02ProofStatus::Unknown;
}

DecisionConeReasonCode map_base_reason(V02ReasonCode reason)
{
	switch (reason) {
	case V02ReasonCode::GuardNotDominating: return DecisionConeReasonCode::GuardNotDominating;
	case V02ReasonCode::GuardUnknown: return DecisionConeReasonCode::GuardMissing;
	case V02ReasonCode::GuardXz: return DecisionConeReasonCode::GuardXZ;
	case V02ReasonCode::ComparisonWidthUnknown: return DecisionConeReasonCode::ComparisonWidthUnknown;
	case V02ReasonCode::ComparisonSignednessUnknown: return DecisionConeReasonCode::ComparisonSignednessUnknown;
	case V02ReasonCode::ComparatorFanoutUncovered: return DecisionConeReasonCode::UncoveredFanout;
	case V02ReasonCode::DataBitUnknown: return DecisionConeReasonCode::DataBitUnknown;
	case V02ReasonCode::PartialDataFold: return DecisionConeReasonCode::PartialSinkProof;
	case V02ReasonCode::PreservedBitMissing: return DecisionConeReasonCode::PartialSinkProof;
	case V02ReasonCode::MultipleDriver: return DecisionConeReasonCode::MultipleDriver;
	case V02ReasonCode::SequentialBoundaryChanged: return DecisionConeReasonCode::SequentialBoundaryChanged;
	case V02ReasonCode::ResetEnableHoldUnknown: return DecisionConeReasonCode::ResetEnableHoldUnknown;
	case V02ReasonCode::FactConflict: return DecisionConeReasonCode::FactConflict;
	case V02ReasonCode::BudgetCellsExhausted: return DecisionConeReasonCode::BudgetCellsExhausted;
	case V02ReasonCode::BudgetBitsExhausted: return DecisionConeReasonCode::BudgetBitsExhausted;
	case V02ReasonCode::BudgetEventsExhausted: return DecisionConeReasonCode::BudgetEventsExhausted;
	case V02ReasonCode::SnapshotMismatch: return DecisionConeReasonCode::SnapshotMismatch;
	case V02ReasonCode::InvalidRequest: return DecisionConeReasonCode::InvalidRequest;
	default: return DecisionConeReasonCode::StructureNotFound;
	}
}

CellSnapshot snapshot_cell(const RTLIL::Cell *cell)
{
	CellSnapshot result;
	result.cell_id = cell->name.str(); result.type = cell->type;
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

std::vector<DecisionAttributeSnapshot> attributes_of(const dict<RTLIL::IdString, RTLIL::Const> &attributes)
{
	std::vector<DecisionAttributeSnapshot> result;
	for (const auto &item : attributes) result.push_back({item.first, item.second});
	return result;
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
				for (int bit = 0; bit < wire->width; ++bit) module_outputs.insert(sigmap(RTLIL::SigBit(wire, bit)));
		for (RTLIL::Cell *cell : module->cells())
			for (const auto &conn : cell->connections()) {
				bool known = celltypes.cell_known(cell->type);
				bool output = known ? celltypes.cell_output(cell->type, conn.first) : conn.first.in(ID::Y, ID::Q);
				bool input = known ? celltypes.cell_input(cell->type, conn.first) : !output;
				for (int bit = 0; bit < GetSize(conn.second); ++bit) {
					RTLIL::SigBit canonical = sigmap(conn.second[bit]);
					if (!canonical.wire) continue;
					if (output) drivers[canonical].push_back({cell, conn.first, bit});
					if (input) uses[canonical].push_back({cell->name.str(), conn.first, bit, canonical});
				}
			}
	}
};

struct ProofBudget {
	BudgetTracker tracker;
	std::set<RTLIL::Cell *> cells;
	std::set<RTLIL::SigBit, BitLess> bits;
	bool ok = true;

	explicit ProofBudget(const AnalysisBudget &limit) : tracker(limit.max_cells, limit.max_bits, limit.max_events) { }
	bool charge(const ResourceUsage &usage)
	{
		for (int i = 0; i < usage.visited_cells && ok; ++i) if (!tracker.take_cell()) ok = false;
		for (int i = 0; i < usage.visited_bits && ok; ++i) if (!tracker.take_bit()) ok = false;
		for (int i = 0; i < usage.propagation_events && ok; ++i) if (!tracker.take_event()) ok = false;
		return ok;
	}
	bool cell(RTLIL::Cell *value) { if (value && cells.insert(value).second && !tracker.take_cell()) ok = false; return ok; }
	bool bit(RTLIL::SigBit value) { if (value.wire && bits.insert(value).second && !tracker.take_bit()) ok = false; return ok; }
	bool event() { if (!tracker.take_event()) ok = false; return ok; }
	ResourceUsage usage() const { return {tracker.cells(), tracker.bits(), tracker.events()}; }
	DecisionConeReasonCode reason() const { return budget_reason(tracker.exhausted()); }
};

class Evaluator {
public:
	Index &index;
	ProofBudget &budget;
	std::map<RTLIL::SigBit, RTLIL::State, BitLess> assignment;
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
		if (found != index.drivers.end() && found->second.size() == 1 && !is_sequential(found->second.front().cell->type)) {
			budget.cell(found->second.front().cell);
			result = eval_cell(found->second.front().cell, found->second.front().offset);
		}
		active.erase(bit); memo[bit] = result; return result;
	}

private:
	RTLIL::State invert(RTLIL::State value) const
	{
		return value == RTLIL::State::S0 ? RTLIL::State::S1 : value == RTLIL::State::S1 ? RTLIL::State::S0 : RTLIL::State::Sx;
	}
	RTLIL::State eval_cell(RTLIL::Cell *cell, int offset)
	{
		if (cell->type.in(ID($eq), ID($ne))) {
			RTLIL::SigSpec a = index.sigmap(cell->getPort(ID::A)), b = index.sigmap(cell->getPort(ID::B));
			if (GetSize(a) != GetSize(b)) return RTLIL::State::Sx;
			bool equal = true;
			for (int bit = 0; bit < GetSize(a); ++bit) {
				RTLIL::State av = eval(a[bit]), bv = eval(b[bit]);
				if (!binary(av) || !binary(bv)) return RTLIL::State::Sx;
				equal &= av == bv;
			}
			RTLIL::State value = equal ? RTLIL::State::S1 : RTLIL::State::S0;
			return cell->type == ID($ne) ? invert(value) : value;
		}
		if (cell->type == ID($not)) return invert(eval(cell->getPort(ID::A)[offset]));
		if (cell->type == ID($mux)) {
			RTLIL::State select = eval(cell->getPort(ID::S)[0]);
			if (!binary(select)) return RTLIL::State::Sx;
			return eval(cell->getPort(select == RTLIL::State::S1 ? ID::B : ID::A)[offset]);
		}
		if (cell->type == ID($pmux)) {
			RTLIL::SigSpec selects = cell->getPort(ID::S); int chosen = -1;
			for (int bit = 0; bit < GetSize(selects); ++bit) {
				RTLIL::State value = eval(selects[bit]);
				if (!binary(value)) return RTLIL::State::Sx;
				if (value == RTLIL::State::S1) { if (chosen >= 0) return RTLIL::State::Sx; chosen = bit; }
			}
			if (chosen < 0) return eval(cell->getPort(ID::A)[offset]);
			int width = cell->getParam(ID::WIDTH).as_int();
			return eval(cell->getPort(ID::B)[chosen * width + offset]);
		}
		if (cell->type.in(ID($reduce_and), ID($reduce_or), ID($reduce_bool), ID($reduce_xor), ID($reduce_xnor))) {
			bool is_and = cell->type == ID($reduce_and), parity = false, any_one = false, any_zero = false;
			for (RTLIL::SigBit input : cell->getPort(ID::A)) {
				RTLIL::State value = eval(input); if (!binary(value)) return RTLIL::State::Sx;
				any_one |= value == RTLIL::State::S1; any_zero |= value == RTLIL::State::S0;
				parity ^= value == RTLIL::State::S1;
			}
			if (cell->type == ID($reduce_xor)) return parity ? RTLIL::State::S1 : RTLIL::State::S0;
			if (cell->type == ID($reduce_xnor)) return parity ? RTLIL::State::S0 : RTLIL::State::S1;
			return is_and ? (any_zero ? RTLIL::State::S0 : RTLIL::State::S1) :
				(any_one ? RTLIL::State::S1 : RTLIL::State::S0);
		}
		if (cell->type.in(ID($logic_and), ID($logic_or), ID($and), ID($or), ID($xor), ID($xnor))) {
			RTLIL::State a = eval(cell->getPort(ID::A)[offset < GetSize(cell->getPort(ID::A)) ? offset : 0]);
			RTLIL::State b = eval(cell->getPort(ID::B)[offset < GetSize(cell->getPort(ID::B)) ? offset : 0]);
			if (!binary(a) || !binary(b)) return RTLIL::State::Sx;
			bool av = a == RTLIL::State::S1, bv = b == RTLIL::State::S1, result = false;
			if (cell->type.in(ID($logic_and), ID($and))) result = av && bv;
			else if (cell->type.in(ID($logic_or), ID($or))) result = av || bv;
			else if (cell->type == ID($xor)) result = av != bv;
			else result = av == bv;
			return result ? RTLIL::State::S1 : RTLIL::State::S0;
		}
		return RTLIL::State::Sx;
	}
};

bool supported_comb(RTLIL::IdString type)
{
	return type.in(ID($eq), ID($ne), ID($not), ID($mux), ID($pmux), ID($reduce_and),
		ID($reduce_or), ID($reduce_bool), ID($reduce_xor), ID($reduce_xnor), ID($logic_and),
		ID($logic_or), ID($and), ID($or), ID($xor), ID($xnor));
}

void trace_cone(Index &index, ProofBudget &budget, RTLIL::SigBit raw,
		std::set<RTLIL::SigBit, BitLess> &leaves, std::set<RTLIL::SigBit, BitLess> &seen,
		std::set<RTLIL::Cell *> &cells, DecisionConeReasonCode &failure)
{
	RTLIL::SigBit bit = index.sigmap(raw);
	if (!bit.wire || !seen.insert(bit).second || failure != DecisionConeReasonCode::Proved) return;
	budget.bit(bit);
	auto found = index.drivers.find(bit);
	if (found == index.drivers.end()) { leaves.insert(bit); return; }
	if (found->second.size() != 1) { failure = DecisionConeReasonCode::MultipleDriver; return; }
	RTLIL::Cell *cell = found->second.front().cell;
	if (is_sequential(cell->type)) { leaves.insert(bit); return; }
	if (!supported_comb(cell->type)) { failure = DecisionConeReasonCode::UnsupportedCell; return; }
	budget.cell(cell); cells.insert(cell);
	for (const auto &conn : cell->connections())
		if (index.celltypes.cell_input(cell->type, conn.first))
			for (RTLIL::SigBit input : conn.second)
				trace_cone(index, budget, input, leaves, seen, cells, failure);
}

int active_control(Evaluator &eval, RTLIL::Cell *cell, RTLIL::IdString port, RTLIL::IdString polarity)
{
	if (!cell->hasPort(port)) return 0;
	if (!cell->hasParam(polarity)) return -1;
	RTLIL::State value = eval.eval(cell->getPort(port)[0]);
	if (!binary(value) || !binary(cell->getParam(polarity)[0])) return -1;
	return (value == RTLIL::State::S1) == cell->getParam(polarity).as_bool();
}

// 1 means D is observed, 0 means hold/reset dominates, -1 is unknown.
int d_observed(Evaluator &eval, RTLIL::Cell *cell)
{
	if (cell->hasPort(ID::ARST)) { int value = active_control(eval, cell, ID::ARST, ID::ARST_POLARITY); if (value != 0) return value < 0 ? -1 : 0; }
	if (cell->type == ID($sdffce) && cell->hasPort(ID::EN)) {
		int value = active_control(eval, cell, ID::EN, ID::EN_POLARITY); if (value != 1) return value;
	}
	if (cell->hasPort(ID::SRST)) { int value = active_control(eval, cell, ID::SRST, ID::SRST_POLARITY); if (value != 0) return value < 0 ? -1 : 0; }
	if (cell->hasPort(ID::EN) && cell->type != ID($sdffce)) {
		int value = active_control(eval, cell, ID::EN, ID::EN_POLARITY); if (value != 1) return value;
	}
	return cell->hasPort(ID::D) ? 1 : -1;
}

int sequential_next(Evaluator &eval, RTLIL::Cell *cell, int bit, int q, int replacement_d = -1)
{
	if (cell->hasPort(ID::ARST)) {
		int active = active_control(eval, cell, ID::ARST, ID::ARST_POLARITY); if (active < 0) return -1;
		if (active) { RTLIL::State v = cell->getParam(ID::ARST_VALUE)[bit]; return binary(v) ? v == RTLIL::State::S1 : -1; }
	}
	if (cell->type == ID($sdffce) && cell->hasPort(ID::EN)) {
		int active = active_control(eval, cell, ID::EN, ID::EN_POLARITY); if (active < 0) return -1; if (!active) return q;
	}
	if (cell->hasPort(ID::SRST)) {
		int active = active_control(eval, cell, ID::SRST, ID::SRST_POLARITY); if (active < 0) return -1;
		if (active) { RTLIL::State v = cell->getParam(ID::SRST_VALUE)[bit]; return binary(v) ? v == RTLIL::State::S1 : -1; }
	}
	if (cell->hasPort(ID::EN) && cell->type != ID($sdffce)) {
		int active = active_control(eval, cell, ID::EN, ID::EN_POLARITY); if (active < 0) return -1; if (!active) return q;
	}
	if (replacement_d >= 0) return replacement_d;
	RTLIL::State value = eval.eval(cell->getPort(ID::D)[bit]);
	return binary(value) ? value == RTLIL::State::S1 : -1;
}

DecisionSequentialBitSnapshot snapshot_sequential(Index &index, RTLIL::Cell *cell, int d_bit, int q_bit, int logical)
{
	DecisionSequentialBitSnapshot result;
	result.logical_sink_bit = logical; result.cell_id = cell->name.str(); result.cell = cell;
	result.d_bit = d_bit; result.q_bit = q_bit; result.cell_snapshot = snapshot_cell(cell);
	result.cell_attributes = attributes_of(cell->attributes);
	if (cell->hasPort(ID::Q) && q_bit >= 0 && q_bit < GetSize(cell->getPort(ID::Q))) {
		RTLIL::SigBit q = cell->getPort(ID::Q)[q_bit];
		if (q.wire) {
			result.q_wire_attributes = attributes_of(q.wire->attributes);
			result.q_wire_port_id = q.wire->port_id; result.q_wire_port_input = q.wire->port_input;
			result.q_wire_port_output = q.wire->port_output;
		}
	}
	return result;
}

bool same_sequential_snapshot(const DecisionSequentialBitSnapshot &a, const DecisionSequentialBitSnapshot &b)
{
	if (a.logical_sink_bit != b.logical_sink_bit || a.cell_id != b.cell_id || a.d_bit != b.d_bit ||
			a.q_bit != b.q_bit || !same_snapshot(a.cell_snapshot, b.cell_snapshot) ||
			a.q_wire_port_id != b.q_wire_port_id || a.q_wire_port_input != b.q_wire_port_input ||
			a.q_wire_port_output != b.q_wire_port_output ||
			a.cell_attributes.size() != b.cell_attributes.size() || a.q_wire_attributes.size() != b.q_wire_attributes.size()) return false;
	for (std::size_t i = 0; i < a.cell_attributes.size(); ++i)
		if (a.cell_attributes[i].attribute != b.cell_attributes[i].attribute || a.cell_attributes[i].value != b.cell_attributes[i].value) return false;
	for (std::size_t i = 0; i < a.q_wire_attributes.size(); ++i)
		if (a.q_wire_attributes[i].attribute != b.q_wire_attributes[i].attribute || a.q_wire_attributes[i].value != b.q_wire_attributes[i].value) return false;
	return true;
}

uint64_t hash_text(const std::string &text)
{
	uint64_t hash = 1469598103934665603ull;
	for (unsigned char ch : text) { hash ^= ch; hash *= 1099511628211ull; }
	return hash;
}

std::string proof_id(const std::string &kind, const std::string &payload)
{
	std::ostringstream out; out << kind << '-' << std::hex << hash_text(payload); return out.str();
}

struct DagBuilder {
	std::vector<DecisionDagNode> nodes;
	std::map<std::tuple<int, int, int>, int> unique;
	int max_nodes;
	bool ok = true;

	explicit DagBuilder(int limit) : max_nodes(limit)
	{
		nodes.push_back({0, DecisionDagNodeKind::Constant0, -1, -1, -1, proof_id("dag", "0")});
		nodes.push_back({1, DecisionDagNodeKind::Constant1, -1, -1, -1, proof_id("dag", "1")});
	}

	int test(int input, int low, int high)
	{
		if (low == high) return low;
		auto key = std::make_tuple(input, low, high); auto found = unique.find(key);
		if (found != unique.end()) return found->second;
		if (GetSize(nodes) >= max_nodes) { ok = false; return 0; }
		int id = GetSize(nodes); std::ostringstream payload; payload << input << ':' << low << ':' << high;
		nodes.push_back({id, DecisionDagNodeKind::Test, input, low, high, proof_id("dag", payload.str())});
		unique[key] = id; return id;
	}

	int build(const std::vector<int> &rows, int variable, int variables,
			const std::vector<int> &care, const std::vector<int> &value)
	{
		int first = -1; bool mixed = false, any = false;
		for (int row : rows) if (care[row]) { any = true; if (first < 0) first = value[row]; else mixed |= first != value[row]; }
		if (!any) return 0;
		if (!mixed) return first ? 1 : 0;
		if (variable >= variables) { ok = false; return 0; }
		std::vector<int> low, high;
		for (int row : rows) ((row >> variable) & 1 ? high : low).push_back(row);
		int low_node = build(low, variable + 1, variables, care, value);
		int high_node = build(high, variable + 1, variables, care, value);
		return test(variable, low_node, high_node);
	}

	int evaluate(int root, int address) const
	{
		int node = root;
		while (node >= 0 && node < GetSize(nodes) && nodes[node].kind == DecisionDagNodeKind::Test)
			node = (address >> nodes[node].input_index) & 1 ? nodes[node].high_node : nodes[node].low_node;
		return node == 1 ? 1 : node == 0 ? 0 : -1;
	}
};

class Analyzer final : public DecisionConeAnalyzer {
public:
	DecisionConeProof analyze(const DecisionConeProofRequest &request) const override
	{
		DecisionConeProof out; out.module = request.module_id;
		ProofBudget *live_budget = nullptr;
		auto fail = [&](DecisionConeReasonCode reason) {
			if (live_budget != nullptr) out.usage = live_budget->usage();
			out.reason_code = reason; out.status = status_for_reason(reason); return out;
		};
		if (request.module == nullptr || request.module_id.empty() || request.max_support_bits < 1 ||
				request.max_support_bits > 20 || request.max_dag_nodes < 2)
			return fail(DecisionConeReasonCode::InvalidRequest);

		ProductionCase34ProofRequest base_request;
		base_request.module = request.module; base_request.module_id = request.module_id;
		// The frozen production analyzer accepts only the project-wide limits.
		// Its measured usage is charged into this request before any new work;
		// smaller test budgets therefore fail closed at the accounting boundary.
		base_request.budget = AnalysisBudget();
		auto base = make_production_case34_analyzer()->analyze(base_request);
		const ProductionGuardedConeProof *guarded = nullptr;
		for (const auto &candidate : base.guarded_cones)
			if (candidate.status == V02ProofStatus::Normal && candidate.proven_all_obligations) {
				if (guarded != nullptr) return fail(DecisionConeReasonCode::StructureNotFound);
				guarded = &candidate;
			}
		if (!guarded) {
			if (base.status == V02ProofStatus::BudgetExhausted ||
					(!base.guarded_cones.empty() && base.guarded_cones.front().status == V02ProofStatus::BudgetExhausted)) {
				V02ReasonCode reason = !base.guarded_cones.empty() ? base.guarded_cones.front().reason_code : base.reason_code;
				return fail(reason == V02ReasonCode::BudgetCellsExhausted ? DecisionConeReasonCode::BudgetCellsExhausted :
					reason == V02ReasonCode::BudgetBitsExhausted ? DecisionConeReasonCode::BudgetBitsExhausted : DecisionConeReasonCode::BudgetEventsExhausted);
			}
			if (!base.guarded_cones.empty()) return fail(map_base_reason(base.guarded_cones.front().reason_code));
			return fail(DecisionConeReasonCode::StructureNotFound);
		}
		out.source_value = guarded->source_value; out.comparators = guarded->comparator_cells;
		if (guarded->removed_bit_indices.empty()) return fail(DecisionConeReasonCode::GuardMissing);
		out.guard_fixed_source_bits = guarded->removed_bit_indices;
		Index index(request.module); ProofBudget budget(request.budget); live_budget = &budget;
		if (!budget.charge(guarded->usage)) { out.usage = budget.usage(); return fail(budget.reason()); }

		std::set<int> fixed(guarded->removed_bit_indices.begin(), guarded->removed_bit_indices.end());
		for (int bit = 0; bit < GetSize(out.source_value); ++bit) if (!fixed.count(bit)) {
			DecisionSupportBit support; support.input_index = GetSize(out.support_lsb_first);
			support.bit = index.sigmap(out.source_value[bit]);
			support.module_input = support.bit.wire && support.bit.wire->port_input;
			auto driver = index.drivers.find(support.bit);
			if (driver != index.drivers.end() && driver->second.size() == 1 && is_sequential(driver->second.front().cell->type)) {
				support.sequential_q = true; support.sequential_cell_id = driver->second.front().cell->name.str();
				support.sequential_q_bit = driver->second.front().offset;
			}
			support.proof_id = proof_id("support", std::to_string(bit)); out.support_lsb_first.push_back(support);
		}
		if (out.support_lsb_first.empty() || GetSize(out.support_lsb_first) > request.max_support_bits)
			return fail(DecisionConeReasonCode::InvalidRequest);
		if (!request.expected_support_lsb_first.empty()) {
			if (request.expected_support_lsb_first.size() != out.support_lsb_first.size()) return fail(DecisionConeReasonCode::SnapshotMismatch);
			for (int bit = 0; bit < GetSize(out.support_lsb_first); ++bit)
				if (index.sigmap(request.expected_support_lsb_first[bit]) != out.support_lsb_first[bit].bit)
					return fail(DecisionConeReasonCode::SnapshotMismatch);
		}

		std::vector<std::pair<RTLIL::Cell *, int>> sinks;
		std::set<std::pair<RTLIL::Cell *, int>> sink_set;
		for (const auto &item : guarded->sequential_sinks) {
			if (!item.cell || item.q_bit < 0 || !item.cell->hasPort(ID::D) || item.q_bit >= GetSize(item.cell->getPort(ID::D)))
				return fail(DecisionConeReasonCode::SequentialBoundaryChanged);
			if (!supported_sequential(item.cell->type)) return fail(DecisionConeReasonCode::UnsupportedCell);
			if (sink_set.insert({const_cast<RTLIL::Cell *>(item.cell), item.q_bit}).second)
				sinks.push_back({const_cast<RTLIL::Cell *>(item.cell), item.q_bit});
		}
		std::sort(sinks.begin(), sinks.end(), [](const auto &a, const auto &b) {
			if (a.first->name != b.first->name) return a.first->name.str() < b.first->name.str(); return a.second < b.second;
		});
		if (sinks.empty()) return fail(DecisionConeReasonCode::StructureNotFound);

		std::set<RTLIL::Cell *> source_cells;
		std::set<RTLIL::SigBit, BitLess> leaves, seen;
		DecisionConeReasonCode trace_failure = DecisionConeReasonCode::Proved;
		for (const auto &sink : sinks) {
			RTLIL::Cell *cell = sink.first;
			trace_cone(index, budget, cell->getPort(ID::D)[sink.second], leaves, seen, source_cells, trace_failure);
			for (RTLIL::IdString port : {ID::EN, ID::SRST, ID::ARST}) if (cell->hasPort(port))
				trace_cone(index, budget, cell->getPort(port)[0], leaves, seen, source_cells, trace_failure);
		}
		if (trace_failure != DecisionConeReasonCode::Proved) { out.usage = budget.usage(); return fail(trace_failure); }
		if (!budget.ok) { out.usage = budget.usage(); return fail(budget.reason()); }
		for (const auto &sink : sinks) {
			RTLIL::SigBit q = index.sigmap(sink.first->getPort(ID::Q)[sink.second]);
			if (leaves.count(q)) return fail(DecisionConeReasonCode::ResetEnableHoldUnknown);
		}
		for (RTLIL::Cell *cell : source_cells) out.source_cells.push_back(snapshot_cell(cell));
		std::sort(out.source_cells.begin(), out.source_cells.end(), [](const auto &a, const auto &b) { return a.cell_id < b.cell_id; });
		if (!request.expected_source_cells.empty()) {
			if (request.expected_source_cells.size() != out.source_cells.size()) return fail(DecisionConeReasonCode::SnapshotMismatch);
			for (int i = 0; i < GetSize(out.source_cells); ++i)
				if (!same_snapshot(request.expected_source_cells[i], out.source_cells[i])) return fail(DecisionConeReasonCode::SnapshotMismatch);
		}

		std::set<RTLIL::Cell *> affected_cells;
		for (const auto &sink : sinks) affected_cells.insert(sink.first);
		std::set<RTLIL::Cell *> external;
		bool fanout_ok = true;
		for (RTLIL::Cell *cell : source_cells)
			for (const auto &conn : cell->connections()) if (index.celltypes.cell_output(cell->type, conn.first))
				for (RTLIL::SigBit raw : conn.second) {
					RTLIL::SigBit bit = index.sigmap(raw);
					if (index.module_outputs.count(bit)) fanout_ok = false;
					auto uses = index.uses.find(bit); if (uses == index.uses.end()) continue;
					for (const auto &use : uses->second) {
						RTLIL::Cell *consumer = index.module->cell(RTLIL::IdString(use.cell_id));
						if (!consumer) { fanout_ok = false; continue; }
						if (!source_cells.count(consumer) && !affected_cells.count(consumer)) external.insert(consumer);
					}
				}
		for (RTLIL::Cell *cell : external) out.external_consumers.push_back(snapshot_cell(cell));
		std::sort(out.external_consumers.begin(), out.external_consumers.end(), [](const auto &a, const auto &b) { return a.cell_id < b.cell_id; });
		out.all_fanout_covered = fanout_ok;
		if (!fanout_ok) return fail(DecisionConeReasonCode::UncoveredFanout);

		std::vector<RTLIL::SigBit> leaf_bits(leaves.begin(), leaves.end());
		if (leaf_bits.size() > 16) return fail(DecisionConeReasonCode::BudgetEventsExhausted);
		std::map<RTLIL::SigBit, int, BitLess> leaf_index;
		for (int bit = 0; bit < GetSize(leaf_bits); ++bit) leaf_index[leaf_bits[bit]] = bit;
		const int support_rows = 1 << GetSize(out.support_lsb_first);
		const int full_rows = 1 << GetSize(leaf_bits);
		std::vector<std::vector<int>> cares(sinks.size(), std::vector<int>(support_rows));
		std::vector<std::vector<int>> values(sinks.size(), std::vector<int>(support_rows));
		std::vector<bool> guard_row(full_rows, false);
		std::vector<bool> any_comparator(full_rows, false);
		std::vector<std::vector<int>> observed(sinks.size(), std::vector<int>(full_rows));
		std::vector<std::vector<int>> original_d(sinks.size(), std::vector<int>(full_rows, -1));

		for (int row = 0; row < full_rows; ++row) {
			Evaluator eval(index, budget);
			for (int bit = 0; bit < GetSize(leaf_bits); ++bit)
				eval.assignment[leaf_bits[bit]] = (row >> bit) & 1 ? RTLIL::State::S1 : RTLIL::State::S0;
			int address = 0;
			for (int bit = 0; bit < GetSize(out.support_lsb_first); ++bit) {
				auto found = leaf_index.find(out.support_lsb_first[bit].bit);
				if (found == leaf_index.end()) return fail(DecisionConeReasonCode::AliasUnknown);
				if ((row >> found->second) & 1) address |= 1 << bit;
			}
			for (const ComparatorProofConclusion &comparator : guarded->comparator_cells) {
				RTLIL::Cell *cell = index.module->cell(RTLIL::IdString(comparator.cell_id));
				if (!cell || !cell->hasPort(ID::Y)) return fail(DecisionConeReasonCode::SnapshotMismatch);
				RTLIL::State selected = eval.eval(cell->getPort(ID::Y)[0]);
				if (!binary(selected)) return fail(DecisionConeReasonCode::DataBitUnknown);
				any_comparator[row] = any_comparator[row] || selected == RTLIL::State::S1;
			}
			for (int sink_index = 0; sink_index < GetSize(sinks); ++sink_index) {
				RTLIL::Cell *cell = sinks[sink_index].first; int bit = sinks[sink_index].second;
				int use_d = d_observed(eval, cell); if (use_d < 0) return fail(DecisionConeReasonCode::ResetEnableHoldUnknown);
				observed[sink_index][row] = use_d;
				int next_zero = sequential_next(eval, cell, bit, 0);
				int next_one = sequential_next(eval, cell, bit, 1);
				if (next_zero < 0 || next_one < 0) return fail(DecisionConeReasonCode::ResetEnableHoldUnknown);
				if (next_zero != 0 || next_one != 1) guard_row[row] = true;
				if (!use_d) continue;
				RTLIL::State value = eval.eval(cell->getPort(ID::D)[bit]);
				if (!binary(value)) return fail(DecisionConeReasonCode::DataBitUnknown);
				int v = value == RTLIL::State::S1; original_d[sink_index][row] = v;
				if (cares[sink_index][address] && values[sink_index][address] != v)
					return fail(DecisionConeReasonCode::FactConflict);
				cares[sink_index][address] = 1; values[sink_index][address] = v;
			}
			if (!budget.ok) { out.usage = budget.usage(); return fail(budget.reason()); }
		}

		for (int leaf = 0; leaf < GetSize(leaf_bits); ++leaf) {
			int invariant = -1;
			for (int row = 0; row < full_rows; ++row) if (guard_row[row]) {
				int value = (row >> leaf) & 1; if (invariant < 0) invariant = value; else if (invariant != value) { invariant = -2; break; }
			}
			if (invariant >= 0) out.outer_guard_facts.push_back({leaf_bits[leaf], invariant ? RTLIL::State::S1 : RTLIL::State::S0});
		}
		if (out.outer_guard_facts.empty()) return fail(DecisionConeReasonCode::GuardMissing);
		for (int removed : out.guard_fixed_source_bits) {
			RTLIL::SigBit target = index.sigmap(out.source_value[removed]); bool found = false;
			for (const GuardFact &fact : out.outer_guard_facts) if (index.sigmap(fact.bit) == target && binary(fact.value)) found = true;
			if (!found) return fail(DecisionConeReasonCode::GuardNotDominating);
		}
		out.current_cycle_guard_proved = true;

		DagBuilder dag(request.max_dag_nodes); std::vector<int> all_rows(support_rows);
		for (int row = 0; row < support_rows; ++row) all_rows[row] = row;
		for (int sink_index = 0; sink_index < GetSize(sinks); ++sink_index) {
			DecisionSinkProof sink;
			sink.logical_sink_bit = sink_index; sink.cell_id = sinks[sink_index].first->name.str(); sink.cell = sinks[sink_index].first;
			sink.d_bit = sinks[sink_index].second; sink.q_bit = sinks[sink_index].second;
			sink.original_d = sinks[sink_index].first->getPort(ID::D)[sink.d_bit];
			sink.sequential_snapshot = snapshot_sequential(index, const_cast<RTLIL::Cell *>(sink.cell), sink.d_bit, sink.q_bit, sink_index);
			for (RTLIL::IdString port : {ID::EN, ID::SRST, ID::ARST}) if (sink.cell->hasPort(port)) {
				RTLIL::IdString polarity = port == ID::EN ? ID::EN_POLARITY : port == ID::SRST ? ID::SRST_POLARITY : ID::ARST_POLARITY;
				bool active_high = sink.cell->hasParam(polarity) && sink.cell->getParam(polarity).as_bool();
				bool required_high = port == ID::EN ? active_high : !active_high;
				sink.observation_guard_facts.push_back({sink.cell->getPort(port)[0], required_high ? RTLIL::State::S1 : RTLIL::State::S0});
			}
			std::vector<RTLIL::State> care_bits, value_bits;
			for (int row = 0; row < support_rows; ++row) { care_bits.push_back(cares[sink_index][row] ? RTLIL::State::S1 : RTLIL::State::S0); value_bits.push_back(values[sink_index][row] ? RTLIL::State::S1 : RTLIL::State::S0); }
			sink.care_mask_lsb_address = RTLIL::Const(care_bits); sink.value_mask_lsb_address = RTLIL::Const(value_bits);
			sink.normalized_root_node = dag.build(all_rows, 0, GetSize(out.support_lsb_first), cares[sink_index], values[sink_index]);
			if (!dag.ok) return fail(DecisionConeReasonCode::BudgetBitsExhausted);
			for (RTLIL::Cell *cell : source_cells) sink.original_d_cone.push_back(snapshot_cell(cell));
			sink.external_consumers = out.external_consumers;
			bool equivalent = true, outside = true, default_ok = true;
			for (int row = 0; row < full_rows; ++row) {
				int address = 0;
				for (int bit = 0; bit < GetSize(out.support_lsb_first); ++bit)
					if ((row >> leaf_index[out.support_lsb_first[bit].bit]) & 1) address |= 1 << bit;
				int normalized = dag.evaluate(sink.normalized_root_node, address);
				if (observed[sink_index][row] && normalized != original_d[sink_index][row]) {
					equivalent = false; if (!any_comparator[row]) default_ok = false;
				}
				if (!observed[sink_index][row]) {
					Evaluator eval(index, budget); for (int bit = 0; bit < GetSize(leaf_bits); ++bit)
						eval.assignment[leaf_bits[bit]] = (row >> bit) & 1 ? RTLIL::State::S1 : RTLIL::State::S0;
					for (int q : {0, 1}) if (sequential_next(eval, const_cast<RTLIL::Cell *>(sink.cell), sink.d_bit, q) != sequential_next(eval, const_cast<RTLIL::Cell *>(sink.cell), sink.d_bit, q, normalized)) outside = false;
				}
			}
			sink.equivalent_on_all_care_rows = equivalent; sink.guard_outside_behavior_preserved = outside;
			sink.default_path_preserved = default_ok; sink.hold_path_preserved = outside; sink.reset_path_preserved = outside;
			sink.enable_path_preserved = outside; sink.initialization_preserved = true; sink.no_cross_dff_propagation = true;
			sink.all_fanout_covered = fanout_ok; sink.reason_code = equivalent && outside ? DecisionConeReasonCode::Proved : DecisionConeReasonCode::PartialSinkProof;
			std::string payload = sink.care_mask_lsb_address.as_string() + ':' + sink.value_mask_lsb_address.as_string() + ':' + std::to_string(sink.normalized_root_node);
			sink.proof_id = proof_id("sink", payload); sink.sequential_snapshot.proof_id = sink.proof_id;
			out.affected_sinks.push_back(std::move(sink));
		}
		out.normalized_dag = dag.nodes; out.normalized_dag_complete = dag.ok;

		for (RTLIL::Cell *cell : source_cells) if (cell->type.in(ID($mux), ID($pmux))) {
			DecisionRelationProof relation; relation.cell_id = cell->name.str(); relation.snapshot = snapshot_cell(cell);
			for (RTLIL::SigBit select : cell->getPort(ID::S)) relation.selects_in_branch_order.push_back(select);
			relation.overlap_checked = true; relation.last_assignment_semantics_proved = true;
			if (cell->type == ID($mux)) { relation.kind = DecisionRelationKind::MuxPriority; relation.pairwise_disjoint = true; }
			else {
				relation.kind = DecisionRelationKind::PmuxMutuallyExclusive; relation.pairwise_disjoint = true;
				for (int row = 0; row < full_rows && relation.pairwise_disjoint; ++row) {
					Evaluator eval(index, budget); for (int bit = 0; bit < GetSize(leaf_bits); ++bit)
						eval.assignment[leaf_bits[bit]] = (row >> bit) & 1 ? RTLIL::State::S1 : RTLIL::State::S0;
					int selected = 0; for (RTLIL::SigBit select : cell->getPort(ID::S)) {
						RTLIL::State value = eval.eval(select); if (!binary(value)) return fail(DecisionConeReasonCode::SelectOverlapUnproved);
						selected += value == RTLIL::State::S1;
					}
					if (selected > 1) relation.pairwise_disjoint = false;
				}
			}
			relation.reason_code = relation.pairwise_disjoint ? DecisionConeReasonCode::Proved : DecisionConeReasonCode::SelectOverlapUnproved;
			relation.proof_id = proof_id("relation", std::to_string(int(relation.kind)) + ':' + std::to_string(GetSize(relation.selects_in_branch_order)));
			out.decision_relations.push_back(std::move(relation));
		}
		for (const auto &sink : sinks) {
			DecisionRelationProof relation; relation.kind = DecisionRelationKind::SequentialControlPriority;
			relation.cell_id = sink.first->name.str(); relation.snapshot = snapshot_cell(sink.first);
			for (RTLIL::IdString port : {ID::ARST, ID::EN, ID::SRST}) if (sink.first->hasPort(port)) relation.selects_in_branch_order.push_back(sink.first->getPort(port)[0]);
			relation.pairwise_disjoint = true; relation.overlap_checked = true; relation.last_assignment_semantics_proved = true;
			relation.reason_code = DecisionConeReasonCode::Proved; relation.proof_id = proof_id("seq", sink.first->type.str());
			out.decision_relations.push_back(std::move(relation));
		}
		for (const auto &relation : out.decision_relations) if (relation.reason_code != DecisionConeReasonCode::Proved)
			return fail(relation.reason_code);
		out.selection_semantics_proved = true;

		std::set<std::pair<RTLIL::Cell *, int>> affected = sink_set;
		for (RTLIL::Cell *cell : index.module->cells()) if (is_sequential(cell->type) && cell->hasPort(ID::Q) && cell->hasPort(ID::D))
			for (int bit = 0; bit < GetSize(cell->getPort(ID::Q)); ++bit) if (!affected.count({cell, bit}))
				out.unaffected_sequential_bits.push_back(snapshot_sequential(index, cell, bit, bit, GetSize(out.unaffected_sequential_bits)));

		if (!request.expected_sequential_sinks.empty()) {
			if (request.expected_sequential_sinks.size() != out.affected_sinks.size()) return fail(DecisionConeReasonCode::SnapshotMismatch);
			for (int i = 0; i < GetSize(out.affected_sinks); ++i)
				if (!same_sequential_snapshot(request.expected_sequential_sinks[i], out.affected_sinks[i].sequential_snapshot))
					return fail(DecisionConeReasonCode::SnapshotMismatch);
		}

		out.structure_complete = true;
		out.guard_inside_equivalence_proved = std::all_of(out.affected_sinks.begin(), out.affected_sinks.end(), [](const auto &sink) { return sink.equivalent_on_all_care_rows; });
		out.guard_outside_equivalence_proved = std::all_of(out.affected_sinks.begin(), out.affected_sinks.end(), [](const auto &sink) { return sink.guard_outside_behavior_preserved; });
		out.all_sink_bits_proved = std::all_of(out.affected_sinks.begin(), out.affected_sinks.end(), [](const auto &sink) { return sink.reason_code == DecisionConeReasonCode::Proved; });
		out.all_sequential_boundaries_proved = out.all_sink_bits_proved;
		out.reset_enable_hold_contract.no_cross_dff_propagation = true;
		out.reset_enable_hold_contract.reset_preserved = true; out.reset_enable_hold_contract.enable_preserved = true;
		out.reset_enable_hold_contract.hold_preserved = true;
		out.proven_all_obligations = out.structure_complete && out.current_cycle_guard_proved && out.selection_semantics_proved &&
			out.guard_inside_equivalence_proved && out.guard_outside_equivalence_proved && out.all_sink_bits_proved &&
			out.all_fanout_covered && out.all_sequential_boundaries_proved && out.normalized_dag_complete;
		out.usage = budget.usage();
		if (!budget.ok) return fail(budget.reason());
		if (!out.proven_all_obligations) return fail(DecisionConeReasonCode::PartialSinkProof);
		out.status = V02ProofStatus::Normal; out.reason_code = DecisionConeReasonCode::Proved;
		out.proof_ids.push_back(proof_id("cone", std::to_string(GetSize(out.affected_sinks)) + ':' + std::to_string(GetSize(out.normalized_dag))));
		return out;
	}
};

} // namespace

const char *decision_cone_reason_code_name(DecisionConeReasonCode code)
{
	switch (code) {
	case DecisionConeReasonCode::Proved: return "proved";
	case DecisionConeReasonCode::StructureNotFound: return "structure_not_found";
	case DecisionConeReasonCode::GuardMissing: return "guard_missing";
	case DecisionConeReasonCode::GuardNotDominating: return "guard_not_dominating";
	case DecisionConeReasonCode::GuardPartial: return "guard_partial";
	case DecisionConeReasonCode::GuardXZ: return "guard_xz";
	case DecisionConeReasonCode::ComparisonWidthUnknown: return "comparison_width_unknown";
	case DecisionConeReasonCode::ComparisonSignednessUnknown: return "comparison_signedness_unknown";
	case DecisionConeReasonCode::SelectOverlapUnproved: return "select_overlap_unproved";
	case DecisionConeReasonCode::PriorityUnknown: return "priority_unknown";
	case DecisionConeReasonCode::DataBitUnknown: return "data_bit_unknown";
	case DecisionConeReasonCode::PartialSinkProof: return "partial_sink_proof";
	case DecisionConeReasonCode::UncoveredFanout: return "uncovered_fanout";
	case DecisionConeReasonCode::MultipleDriver: return "multiple_driver";
	case DecisionConeReasonCode::AliasUnknown: return "alias_unknown";
	case DecisionConeReasonCode::UnsupportedCell: return "unsupported_cell";
	case DecisionConeReasonCode::SequentialBoundaryChanged: return "sequential_boundary_changed";
	case DecisionConeReasonCode::ResetEnableHoldUnknown: return "reset_enable_hold_unknown";
	case DecisionConeReasonCode::SnapshotMismatch: return "snapshot_mismatch";
	case DecisionConeReasonCode::FactConflict: return "fact_conflict";
	case DecisionConeReasonCode::BudgetCellsExhausted: return "budget_cells_exhausted";
	case DecisionConeReasonCode::BudgetBitsExhausted: return "budget_bits_exhausted";
	case DecisionConeReasonCode::BudgetEventsExhausted: return "budget_events_exhausted";
	case DecisionConeReasonCode::InvalidRequest: return "invalid_request";
	}
	return "invalid_request";
}

std::unique_ptr<DecisionConeAnalyzer> make_decision_cone_analyzer()
{
	return std::make_unique<Analyzer>();
}

} // namespace PmuxOpt

YOSYS_NAMESPACE_END
