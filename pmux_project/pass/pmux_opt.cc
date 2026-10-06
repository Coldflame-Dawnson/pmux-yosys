#include "kernel/yosys.h"
#include "kernel/sigtools.h"
#include "pmux_opt_api.h"
#include "rewrite_plan.h"

#include <algorithm>
#include <string>
#include <vector>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

using namespace PmuxOpt;

enum class StubMode {
	Unknown,
	Zero,
	One,
	Alternating,
};

class FixedAnalyzer final : public EntryAnalyzer
{
public:
	FixedAnalyzer(StubMode mode, bool report_constants) :
		mode_(mode), report_constants_(report_constants) { }

	EntryAnalysisResult analyze(const EntryRequest &request) const override
	{
		EntryAnalysisResult result;
		result.status = EntryStatus::Normal;
		result.reason_code = mode_ == StubMode::Unknown ? "stub_unknown" : "testing_stub";

		for (int index = 0; index < GetSize(request.data); ++index) {
			const RTLIL::SigBit bit = request.data[index];
			if (bit.wire == nullptr && !report_constants_)
				continue;

			BitConclusion conclusion;
			conclusion.bit_index = index;
			if (mode_ == StubMode::Zero)
				conclusion.value = FactValue::Zero;
			else if (mode_ == StubMode::One)
				conclusion.value = FactValue::One;
			else if (mode_ == StubMode::Alternating)
				conclusion.value = ((request.physical_offset + index) & 1) ?
						FactValue::One : FactValue::Zero;
			else
				conclusion.value = FactValue::Unknown;
			conclusion.proof_id = conclusion.value == FactValue::Unknown ? -1 : index + 1;
			conclusion.reason_code = mode_ == StubMode::Unknown ?
					"stub_unknown" : "testing_stub";
			result.bits.push_back(conclusion);
		}

		return result;
	}

private:
	StubMode mode_;
	bool report_constants_;
};

static const char *entry_kind_name(EntryKind kind)
{
	switch (kind) {
	case EntryKind::MuxA: return "mux.A";
	case EntryKind::MuxB: return "mux.B";
	case EntryKind::PmuxA: return "pmux.A";
	case EntryKind::PmuxB: return "pmux.B";
	}
	return "invalid";
}

static bool validate_mux_cell(RTLIL::Cell *cell, int &width, int &s_width, std::string &reason)
{
	if (!cell->hasParam(ID::WIDTH)) {
		reason = "missing_width";
		return false;
	}

	width = cell->getParam(ID::WIDTH).as_int();
	if (width <= 0) {
		reason = "invalid_width";
		return false;
	}

	if (!cell->hasPort(ID::A) || !cell->hasPort(ID::B) ||
			!cell->hasPort(ID::S) || !cell->hasPort(ID::Y)) {
		reason = "missing_port";
		return false;
	}

	if (cell->type == ID($mux)) {
		s_width = 1;
		if (GetSize(cell->getPort(ID::A)) != width ||
				GetSize(cell->getPort(ID::B)) != width ||
				GetSize(cell->getPort(ID::S)) != 1 ||
				GetSize(cell->getPort(ID::Y)) != width) {
			reason = "port_width_mismatch";
			return false;
		}
		return true;
	}

	if (!cell->hasParam(ID::S_WIDTH)) {
		reason = "missing_s_width";
		return false;
	}

	s_width = cell->getParam(ID::S_WIDTH).as_int();
	if (s_width <= 0) {
		reason = "invalid_s_width";
		return false;
	}

	if (GetSize(cell->getPort(ID::A)) != width ||
			GetSize(cell->getPort(ID::B)) != width * s_width ||
			GetSize(cell->getPort(ID::S)) != s_width ||
			GetSize(cell->getPort(ID::Y)) != width) {
		reason = "port_width_mismatch";
		return false;
	}

	return true;
}

static EntryRequest make_request(RTLIL::Module *module, RTLIL::Cell *cell,
		PhysicalPort port, EntryKind kind, int branch_index, int physical_offset,
		const RTLIL::SigSpec &data, const RTLIL::SigSpec &snapshot,
		const std::vector<GuardFact> &guard)
{
	EntryRequest request;
	request.module_id = module->name.str();
	request.cell_id = cell->name.str();
	request.module = module;
	request.cell = cell;
	request.physical_port = port;
	request.entry_kind = kind;
	request.branch_index = branch_index;
	request.physical_offset = physical_offset;
	request.width = GetSize(data);
	request.data = data;
	request.physical_port_snapshot = snapshot;
	request.guard = guard;
	return request;
}

struct PmuxOptPass : public Pass
{
	PmuxOptPass() : Pass("pmux_opt", "enumerate mux/pmux entries for guarded local analysis") { }

	void help() override
	{
		log("\n");
		log("    pmux_opt [-verbose] [-stub-zero|-stub-one|-stub-alternating]\n");
		log("             [-test-rewrite-duplicate] [-test-rewrite-conflict]\n");
		log("             [-test-snapshot-mismatch]\n");
		log("             [-test-report-constants] [selection]\n");
		log("\n");
		log("Enumerate selected, structurally valid $mux and $pmux cells in stable\n");
		log("order, construct branch-local guards, and call the analysis interface.\n");
		log("By default the M1 scaffold uses an UNKNOWN-only analyzer and does not\n");
		log("change RTLIL. The -stub-* modes force test-only conclusions and exercise\n");
		log("the rewrite planner; they must never be used for production synthesis.\n");
		log("-test-rewrite-duplicate injects matching test metadata for one target.\n");
		log("-test-rewrite-conflict injects opposing test metadata for one target bit.\n");
		log("-test-snapshot-mismatch corrupts plan metadata, not RTLIL, to verify that\n");
		log("a failed all-batch precheck performs zero writes.\n");
		log("-test-report-constants makes a forced stub report original constants so\n");
		log("the planner's defensive constant/x/z filter can be verified.\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		bool verbose = false;
		bool test_rewrite_duplicate = false;
		bool test_rewrite_conflict = false;
		bool test_snapshot_mismatch = false;
		bool test_report_constants = false;
		StubMode stub_mode = StubMode::Unknown;
		bool stub_mode_set = false;
		int argidx;
		for (argidx = 1; argidx < GetSize(args); ++argidx) {
			if (args[argidx] == "-verbose") {
				verbose = true;
				continue;
			}
			if (args[argidx] == "-test-snapshot-mismatch") {
				test_snapshot_mismatch = true;
				continue;
			}
			if (args[argidx] == "-test-rewrite-conflict") {
				test_rewrite_conflict = true;
				continue;
			}
			if (args[argidx] == "-test-rewrite-duplicate") {
				test_rewrite_duplicate = true;
				continue;
			}
			if (args[argidx] == "-test-report-constants") {
				test_report_constants = true;
				continue;
			}
			StubMode requested_mode;
			if (args[argidx] == "-stub-zero")
				requested_mode = StubMode::Zero;
			else if (args[argidx] == "-stub-one")
				requested_mode = StubMode::One;
			else if (args[argidx] == "-stub-alternating")
				requested_mode = StubMode::Alternating;
			else
				break;
			if (stub_mode_set)
				log_cmd_error("pmux_opt: select at most one -stub-* mode.\n");
			stub_mode = requested_mode;
			stub_mode_set = true;
			continue;
		}
		extra_args(args, argidx, design);
		if (test_snapshot_mismatch && !stub_mode_set)
			log_cmd_error("pmux_opt: -test-snapshot-mismatch requires a -stub-* mode.\n");
		if (test_rewrite_conflict && !stub_mode_set)
			log_cmd_error("pmux_opt: -test-rewrite-conflict requires a -stub-* mode.\n");
		if (test_rewrite_duplicate && !stub_mode_set)
			log_cmd_error("pmux_opt: -test-rewrite-duplicate requires a -stub-* mode.\n");
		if (test_report_constants && !stub_mode_set)
			log_cmd_error("pmux_opt: -test-report-constants requires a -stub-* mode.\n");

		log_header(design, "Executing PMUX_OPT read-only M1 scaffold.\n");

		std::vector<RTLIL::Module *> modules = design->selected_modules();
		std::sort(modules.begin(), modules.end(), [](const RTLIL::Module *lhs, const RTLIL::Module *rhs) {
			return lhs->name.str() < rhs->name.str();
		});

		FixedAnalyzer analyzer(stub_mode, test_report_constants);
		RewriteBatch rewrite_batch;
		int selected_modules = 0;
		int candidate_cells = 0;
		int valid_cells = 0;
		int invalid_cells = 0;
		int entry_count = 0;
		int unknown_bits = 0;
		int proven_bits = 0;

		for (RTLIL::Module *module : modules) {
			if (module->get_bool_attribute(ID::blackbox))
				continue;
			selected_modules++;

			std::vector<RTLIL::Cell *> cells = module->selected_cells();
			std::sort(cells.begin(), cells.end(), [](const RTLIL::Cell *lhs, const RTLIL::Cell *rhs) {
				return lhs->name.str() < rhs->name.str();
			});

			for (RTLIL::Cell *cell : cells) {
				if (!cell->type.in(ID($mux), ID($pmux)))
					continue;
				candidate_cells++;

				int width = 0;
				int s_width = 0;
				std::string invalid_reason;
				if (cell->get_bool_attribute(ID::keep))
					invalid_reason = "keep_attribute";
				else if (!validate_mux_cell(cell, width, s_width, invalid_reason)) {
					// The validator provides a stable reason code.
				}

				if (!invalid_reason.empty()) {
					invalid_cells++;
					log_warning("pmux_opt: skip %s.%s (%s).\n",
							log_id(module), log_id(cell), invalid_reason.c_str());
					continue;
				}
				valid_cells++;

				const RTLIL::SigSpec port_a = cell->getPort(ID::A);
				const RTLIL::SigSpec port_b = cell->getPort(ID::B);
				const RTLIL::SigSpec port_s = cell->getPort(ID::S);
				std::vector<EntryRequest> requests;

				std::vector<GuardFact> guard_a;
				for (int select_index = 0; select_index < s_width; ++select_index)
					guard_a.push_back({port_s[select_index], RTLIL::State::S0});
				requests.push_back(make_request(module, cell, PhysicalPort::A,
						cell->type == ID($mux) ? EntryKind::MuxA : EntryKind::PmuxA,
						-1, 0, port_a, port_a, guard_a));

				for (int branch = 0; branch < s_width; ++branch) {
					std::vector<GuardFact> guard_b = {
						{port_s[branch], RTLIL::State::S1}
					};
					const int offset = branch * width;
					requests.push_back(make_request(module, cell, PhysicalPort::B,
							cell->type == ID($mux) ? EntryKind::MuxB : EntryKind::PmuxB,
							branch, offset, port_b.extract(offset, width), port_b, guard_b));
				}

				for (const EntryRequest &request : requests) {
					EntryAnalysisResult result = analyzer.analyze(request);
					entry_count++;
					for (const BitConclusion &conclusion : result.bits) {
						if (conclusion.value == FactValue::Unknown)
							unknown_bits++;
						else
							proven_bits++;
					}
					rewrite_batch.add(request, result);
					if (verbose) {
						log("pmux_opt: entry module=%s cell=%s kind=%s branch=%d offset=%d width=%d guard_facts=%zu result_bits=%zu status=normal\n",
								request.module_id.c_str(), request.cell_id.c_str(),
								entry_kind_name(request.entry_kind), request.branch_index,
								request.physical_offset, request.width, request.guard.size(),
								result.bits.size());
					}
				}
			}
		}

		if (test_rewrite_duplicate)
			rewrite_batch.inject_first_duplicate_for_test();
		if (test_rewrite_conflict)
			rewrite_batch.inject_first_conflict_for_test();
		if (test_snapshot_mismatch)
			rewrite_batch.corrupt_first_snapshot_for_test();
		std::string commit_failure;
		const bool commit_ok = rewrite_batch.commit(commit_failure);
		if (!commit_ok)
			log_warning("pmux_opt: all-batch commit aborted (%s); writes=0.\n",
					commit_failure.c_str());

		const RewriteStats &rewrite_stats = rewrite_batch.stats();
		log("pmux_opt: rewrite proven=%zu skipped_constants=%zu duplicates=%zu conflicts=%zu abandoned_entries=%zu planned=%zu writes=%zu commit=%s\n",
				rewrite_stats.proven_bits, rewrite_stats.skipped_original_constants,
				rewrite_stats.duplicate_targets, rewrite_stats.rewrite_conflicts,
				rewrite_stats.abandoned_entries, rewrite_stats.planned_bits,
				rewrite_stats.written_bits, commit_ok ? "ok" : "aborted");
		log("pmux_opt: summary modules=%d candidates=%d valid=%d invalid=%d entries=%d unknown_bits=%d proven_bits=%d writes=%zu\n",
				selected_modules, candidate_cells, valid_cells, invalid_cells,
				entry_count, unknown_bits, proven_bits, rewrite_stats.written_bits);
	}
} PmuxOptPass;

PRIVATE_NAMESPACE_END
