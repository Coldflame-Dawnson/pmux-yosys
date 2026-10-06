#include "kernel/yosys.h"
#include "kernel/json.h"
#include "kernel/sigtools.h"
#include "pmux_opt_api.h"
#include "readonly_analyzer.h"
#include "rewrite_plan.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#include <sys/time.h>
#endif

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

using namespace PmuxOpt;

struct ResourceSample {
	long long user_us = 0;
	long long system_us = 0;
	long long peak_kib = 0;
	bool available = false;
};

static ResourceSample sample_resources()
{
	ResourceSample sample;
#if defined(__unix__) || defined(__APPLE__)
	struct rusage usage;
	if (getrusage(RUSAGE_SELF, &usage) == 0) {
		sample.user_us = 1000000LL * usage.ru_utime.tv_sec + usage.ru_utime.tv_usec;
		sample.system_us = 1000000LL * usage.ru_stime.tv_sec + usage.ru_stime.tv_usec;
#if defined(__APPLE__)
		sample.peak_kib = usage.ru_maxrss / 1024;
#else
		sample.peak_kib = usage.ru_maxrss;
#endif
		sample.available = true;
	}
#endif
	return sample;
}

static const char *status_name(EntryStatus status)
{
	switch (status) {
	case EntryStatus::Normal: return "normal";
	case EntryStatus::FactConflict: return "fact_conflict";
	case EntryStatus::BudgetExhausted: return "budget_exhausted";
	case EntryStatus::InvalidRequest: return "invalid_request";
	}
	return "invalid_status";
}

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

static void write_json_file(std::string filename, const Json &document)
{
	rewrite_filename(filename);
	std::ofstream stream(filename, std::ofstream::out | std::ofstream::trunc);
	if (!stream)
		log_cmd_error("pmux_opt: cannot open JSON output file `%s'.\n", filename.c_str());
	stream << document.dump() << "\n";
	stream.close();
	if (!stream)
		log_cmd_error("pmux_opt: failed writing JSON output file `%s'.\n", filename.c_str());
}

static Json build_plan_json(const RewriteBatch &batch, bool commit_ok,
		const std::string &commit_failure, const std::string &mode)
{
	Json::array edits;
	for (const CommittedEdit &edit : batch.committed_edits()) {
		Json branch = edit.branch_index < 0 ? Json(nullptr) : Json(edit.branch_index);
		Json old_value = Json::object {
			{"kind", "signal"},
			{"wire", RTLIL::unescape_id(edit.original.wire->name)},
			{"offset", edit.original.offset},
		};
		edits.push_back(Json::object {
			{"module", edit.module_id},
			{"cell", edit.cell_id},
			{"port", edit.physical_port == PhysicalPort::A ? "A" : "B"},
			{"bit", edit.physical_bit},
			{"value", edit.target == RTLIL::State::S0 ? "0" : "1"},
			{"old", old_value},
			{"branch", branch},
			{"branch_bit", edit.branch_bit},
			{"proof_id", edit.proof_id},
			{"source_entry", edit.source_entry_id},
		});
	}

	Json::array abandoned_entries;
	for (const AbandonedEntry &entry : batch.abandoned_entries())
		abandoned_entries.push_back(Json::object {
			{"entry", entry.entry_id},
			{"reason", entry.reason_code},
		});

	return Json::object {
		{"schema", 1},
		{"producer", "pmux_opt"},
		{"mode", mode},
		{"commit", commit_ok ? "ok" : "aborted"},
		{"commit_reason", commit_failure.empty() ? Json(nullptr) : Json(commit_failure)},
		{"edits", edits},
		{"abandoned_entries", abandoned_entries},
	};
}

static Json build_stats_json(const std::string &mode,
		int selected_modules, int candidate_cells, int valid_cells, int invalid_cells,
		int entry_count, int unknown_bits, int proven_zero, int proven_one,
		std::size_t visited_cells, std::size_t visited_bits, std::size_t propagation_events,
		const RewriteStats &rewrite_stats, const std::map<std::string, int> &invalid_reasons,
		const std::map<std::string, int> &entry_statuses,
		const std::map<std::string, int> &unknown_reasons,
		bool commit_ok, const std::string &commit_failure, long long wall_us,
		const ResourceSample &resources_start, const ResourceSample &resources_end)
{
	std::map<std::string, int> reason_counts;
	for (const auto &item : invalid_reasons)
		reason_counts[item.first] += item.second;
	for (const auto &item : entry_statuses)
		if (item.first != "normal")
			reason_counts[item.first] += item.second;
	for (const auto &item : unknown_reasons)
		reason_counts[item.first] += item.second;
	if (rewrite_stats.rewrite_conflicts != 0)
		reason_counts["rewrite_conflict"] += int(rewrite_stats.rewrite_conflicts);
	if (rewrite_stats.skipped_original_binary_constants != 0)
		reason_counts["original_binary_constant"] += int(rewrite_stats.skipped_original_binary_constants);
	if (rewrite_stats.skipped_original_xz != 0)
		reason_counts["original_xz"] += int(rewrite_stats.skipped_original_xz);
	if (!commit_ok)
		reason_counts[commit_failure.empty() ? "commit_aborted" : commit_failure]++;

	Json::object reason_count_json;
	Json::array reason_codes;
	for (const auto &item : reason_counts) {
		reason_count_json[item.first] = item.second;
		reason_codes.push_back(item.first);
	}

	Json::object entry_status_json;
	for (const auto &item : entry_statuses)
		entry_status_json[item.first] = item.second;

	Json resources;
	if (resources_start.available && resources_end.available) {
		resources = Json::object {
			{"wall_us", double(wall_us)},
			{"user_us", double(resources_end.user_us - resources_start.user_us)},
			{"system_us", double(resources_end.system_us - resources_start.system_us)},
			{"peak_kib", double(resources_end.peak_kib)},
			{"peak_growth_kib", double(std::max(0LL,
					resources_end.peak_kib - resources_start.peak_kib))},
			{"time_unit", "microseconds"},
			{"memory_unit", "KiB"},
		};
	} else {
		resources = Json::object {
			{"wall_us", double(wall_us)},
			{"os_counters", "unavailable"},
			{"time_unit", "microseconds"},
		};
	}

	return Json::object {
		{"schema", 1},
		{"producer", "pmux_opt"},
		{"mode", mode},
		{"changed_bits", int(rewrite_stats.written_bits)},
		{"reason_codes", reason_codes},
		{"reason_counts", reason_count_json},
		{"scan", Json::object {
			{"modules", selected_modules},
			{"candidate_cells", candidate_cells},
			{"valid_cells", valid_cells},
			{"invalid_cells", invalid_cells},
			{"entries", entry_count},
			{"target_bits", unknown_bits + proven_zero + proven_one},
		}},
		{"analysis", Json::object {
			{"entry_status", entry_status_json},
			{"visited_cells", int(visited_cells)},
			{"visited_bits", int(visited_bits)},
			{"propagation_events", int(propagation_events)},
			{"proven_zero", proven_zero},
			{"proven_one", proven_one},
			{"unknown_bits", unknown_bits},
			{"sat_queries", 0},
		}},
		{"rewrite", Json::object {
			{"candidate_bits", int(rewrite_stats.proven_bits)},
			{"accepted_bits", int(rewrite_stats.planned_bits)},
			{"committed_bits", int(rewrite_stats.written_bits)},
			{"skipped_original_binary_constants", int(rewrite_stats.skipped_original_binary_constants)},
			{"skipped_original_xz", int(rewrite_stats.skipped_original_xz)},
			{"rejected_results", int(rewrite_stats.rejected_results)},
			{"rejected_unproved_conclusions", int(rewrite_stats.rejected_unproved_conclusions)},
			{"deduplicated_targets", int(rewrite_stats.duplicate_targets)},
			{"rewrite_conflicts", int(rewrite_stats.rewrite_conflicts)},
			{"abandoned_entries", int(rewrite_stats.abandoned_entries)},
			{"commit", commit_ok ? "ok" : "aborted"},
			{"commit_reason", commit_failure.empty() ? Json(nullptr) : Json(commit_failure)},
		}},
		{"resources", resources},
	};
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
	if (s_width > INT_MAX / width) {
		reason = "port_width_overflow";
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
		log("    pmux_opt [-verbose] [-stub-unknown|-stub-zero|-stub-one|-stub-alternating]\n");
		log("             [-test-rewrite-duplicate] [-test-rewrite-conflict]\n");
		log("             [-test-snapshot-mismatch]\n");
		log("             [-test-report-constants]\n");
		log("             [-write-plan <file>] [-write-stats <file>] [selection]\n");
		log("\n");
		log("Enumerate selected, structurally valid $mux and $pmux cells in stable\n");
		log("order, construct branch-local guards, and call the analysis interface.\n");
		log("By default the Pass uses the read-only proof analyzer. The -stub-* modes\n");
		log("select test-only conclusions and exercise\n");
		log("the rewrite planner; they must never be used for production synthesis.\n");
		log("-test-rewrite-duplicate injects matching test metadata for one target.\n");
		log("-test-rewrite-conflict injects opposing test metadata for one target bit.\n");
		log("-test-snapshot-mismatch corrupts plan metadata, not RTLIL, to verify that\n");
		log("a failed all-batch precheck performs zero writes.\n");
		log("-test-report-constants makes a forced stub report original constants so\n");
		log("the planner's defensive constant/x/z filter can be verified.\n");
		log("-write-plan and -write-stats emit schema-1 machine-readable JSON.\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		const auto wall_start = std::chrono::steady_clock::now();
		const ResourceSample resources_start = sample_resources();
		bool verbose = false;
		bool test_rewrite_duplicate = false;
		bool test_rewrite_conflict = false;
		bool test_snapshot_mismatch = false;
		bool test_report_constants = false;
		StubMode stub_mode = StubMode::Unknown;
		bool stub_mode_set = false;
		std::string plan_json_file;
		std::string stats_json_file;
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
			if (args[argidx] == "-write-plan" && argidx + 1 < GetSize(args)) {
				plan_json_file = args[++argidx];
				continue;
			}
			if (args[argidx] == "-write-stats" && argidx + 1 < GetSize(args)) {
				stats_json_file = args[++argidx];
				continue;
			}
			StubMode requested_mode;
			if (args[argidx] == "-stub-unknown")
				requested_mode = StubMode::Unknown;
			else if (args[argidx] == "-stub-zero")
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

		log_header(design, "Executing PMUX_OPT guarded analysis and atomic rewrite.\n");

		std::vector<RTLIL::Module *> modules = design->selected_modules();
		std::sort(modules.begin(), modules.end(), [](const RTLIL::Module *lhs, const RTLIL::Module *rhs) {
			return lhs->name.str() < rhs->name.str();
		});

		std::unique_ptr<EntryAnalyzer> analyzer;
		if (stub_mode_set)
			analyzer.reset(new FixedAnalyzer(stub_mode, test_report_constants));
		else
			analyzer = make_readonly_entry_analyzer();
		RewriteBatch rewrite_batch;
		int selected_modules = 0;
		int candidate_cells = 0;
		int valid_cells = 0;
		int invalid_cells = 0;
		int entry_count = 0;
		int unknown_bits = 0;
		int proven_bits = 0;
		int proven_zero = 0;
		int proven_one = 0;
		std::size_t visited_cells = 0;
		std::size_t visited_bits = 0;
		std::size_t propagation_events = 0;
		std::map<std::string, int> invalid_reason_counts;
		std::map<std::string, int> analysis_status_counts;
		std::map<std::string, int> unknown_reason_counts;

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
					invalid_reason_counts[invalid_reason]++;
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
							cell->type == ID($mux) ? -1 : branch, offset,
							port_b.extract(offset, width), port_b, guard_b));
				}

				for (const EntryRequest &request : requests) {
					EntryAnalysisResult result = analyzer->analyze(request);
					entry_count++;
					analysis_status_counts[status_name(result.status)]++;
					visited_cells += result.usage.visited_cells;
					visited_bits += result.usage.visited_bits;
					propagation_events += result.usage.propagation_events;
					for (const BitConclusion &conclusion : result.bits) {
						if (conclusion.value == FactValue::Unknown) {
							unknown_bits++;
							unknown_reason_counts[conclusion.reason_code.empty() ?
									"unspecified_unknown" : conclusion.reason_code]++;
						} else {
							proven_bits++;
							if (conclusion.value == FactValue::Zero)
								proven_zero++;
							else
								proven_one++;
						}
					}
					rewrite_batch.add(request, result);
					if (verbose) {
						log("pmux_opt: entry module=%s cell=%s kind=%s branch=%d offset=%d width=%d guard_facts=%zu result_bits=%zu status=%s\n",
								request.module_id.c_str(), request.cell_id.c_str(),
								entry_kind_name(request.entry_kind), request.branch_index,
								request.physical_offset, request.width, request.guard.size(),
								result.bits.size(), status_name(result.status));
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
		log("pmux_opt: rewrite proven=%zu skipped_binary_constants=%zu skipped_xz=%zu rejected_results=%zu rejected_unproved=%zu duplicates=%zu conflicts=%zu abandoned_entries=%zu planned=%zu writes=%zu commit=%s\n",
				rewrite_stats.proven_bits, rewrite_stats.skipped_original_binary_constants,
				rewrite_stats.skipped_original_xz,
				rewrite_stats.rejected_results, rewrite_stats.rejected_unproved_conclusions,
				rewrite_stats.duplicate_targets,
				rewrite_stats.rewrite_conflicts,
				rewrite_stats.abandoned_entries, rewrite_stats.planned_bits,
				rewrite_stats.written_bits, commit_ok ? "ok" : "aborted");
		for (const auto &item : invalid_reason_counts)
			log("pmux_opt: reason scope=cell code=%s count=%d\n",
					item.first.c_str(), item.second);
		for (const auto &item : analysis_status_counts)
			log("pmux_opt: reason scope=entry_status code=%s count=%d\n",
					item.first.c_str(), item.second);
		for (const auto &item : unknown_reason_counts)
			log("pmux_opt: reason scope=unknown_bit code=%s count=%d\n",
					item.first.c_str(), item.second);
		log("pmux_opt: summary modules=%d candidates=%d valid=%d invalid=%d entries=%d unknown_bits=%d proven_bits=%d writes=%zu\n",
				selected_modules, candidate_cells, valid_cells, invalid_cells,
				entry_count, unknown_bits, proven_bits, rewrite_stats.written_bits);

		const ResourceSample resources_end = sample_resources();
		const long long wall_us = std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - wall_start).count();
		if (resources_start.available && resources_end.available) {
			const long long peak_growth_kib = std::max(0LL,
					resources_end.peak_kib - resources_start.peak_kib);
			log("pmux_opt: resources wall_us=%lld user_us=%lld system_us=%lld peak_kib=%lld peak_growth_kib=%lld\n",
					wall_us, resources_end.user_us - resources_start.user_us,
					resources_end.system_us - resources_start.system_us,
					resources_end.peak_kib, peak_growth_kib);
		} else {
			log("pmux_opt: resources wall_us=%lld os_counters=unavailable\n", wall_us);
		}

		const std::string machine_mode = stub_mode_set ?
				(stub_mode == StubMode::Unknown ? "testing_stub_unknown" : "testing_stub") :
				"real";
		if (!plan_json_file.empty())
			write_json_file(plan_json_file,
					build_plan_json(rewrite_batch, commit_ok, commit_failure, machine_mode));
		if (!stats_json_file.empty())
			write_json_file(stats_json_file,
					build_stats_json(machine_mode, selected_modules, candidate_cells,
							valid_cells, invalid_cells, entry_count, unknown_bits,
							proven_zero, proven_one, visited_cells, visited_bits,
							propagation_events, rewrite_stats, invalid_reason_counts,
							analysis_status_counts, unknown_reason_counts, commit_ok,
							commit_failure, wall_us, resources_start, resources_end));
	}
} PmuxOptPass;

PRIVATE_NAMESPACE_END
