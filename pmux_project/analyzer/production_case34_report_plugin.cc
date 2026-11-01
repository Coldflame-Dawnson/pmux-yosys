#include "pmux_v02_production_proof_api.h"

#include "backends/rtlil/rtlil_backend.h"
#include "kernel/log.h"
#include "kernel/register.h"

#include <sstream>
#include <set>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {
namespace {

std::string fingerprint(RTLIL::Module *module, RTLIL::Design *design)
{
	std::ostringstream out;
	RTLIL_BACKEND::dump_module(out, "", module, design, false);
	return out.str();
}

void log_snapshot(const char *scope, const CellSnapshot &snapshot)
{
	log("PRODUCTION_CELL_SNAPSHOT scope=%s cell=%s type=%s ports=%d params=%d\n", scope,
		snapshot.cell_id.c_str(), snapshot.type.str().c_str(), GetSize(snapshot.ports), GetSize(snapshot.parameters));
	for (const CellPortSnapshot &port : snapshot.ports)
		log("PRODUCTION_CELL_PORT scope=%s cell=%s port=%s signal=%s\n", scope, snapshot.cell_id.c_str(),
			port.port.str().c_str(), log_signal(port.signal));
	for (const CellParameterSnapshot &param : snapshot.parameters)
		log("PRODUCTION_CELL_PARAM scope=%s cell=%s param=%s value=%s\n", scope, snapshot.cell_id.c_str(),
			param.parameter.str().c_str(), log_signal(RTLIL::SigSpec(param.value)));
}

struct ProductionCase34ReportPass : public Pass {
	ProductionCase34ReportPass() : Pass("pmux_case34_production_proof_report",
			"emit read-only case3/case4 proofs at the frozen production insertion point") { }
	void help() override
	{
		log("    pmux_case34_production_proof_report\n\n");
		log("Discover production guarded cones and bit-sliced constant tables without modifying RTLIL.\n");
	}
	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		log_header(design, "Executing PMUX production case3/case4 proof report.\n");
		extra_args(args, 1, design);
		for (RTLIL::Module *module : design->selected_modules()) {
			std::string before = fingerprint(module, design);
			ProductionCase34ProofRequest request;
			request.module_id = module->name.str(); request.module = module;
			ProductionCase34ProofBundle bundle = make_production_case34_analyzer()->analyze(request);
			for (const ProductionGuardedConeProof &proof : bundle.guarded_cones) {
				log("PRODUCTION_GUARD_PROOF module=%s source_width=%d removed=%d comparators=%d folds=%d preserved=%d guard_cells=%d affected=%d sinks=%d status=%d all_cmp=%d all_data=%d fanout=%d boundaries=%d all=%d reason=%s cells=%d bits=%d events=%d\n",
					proof.module.c_str(), proof.source_width, GetSize(proof.removed_bit_indices), GetSize(proof.comparator_cells),
					GetSize(proof.folded_data_bits), GetSize(proof.preserved_variable_bits), GetSize(proof.guard_logic_cells),
					GetSize(proof.affected_cells), GetSize(proof.sequential_sinks), int(proof.status),
					proof.all_comparators_equivalent, proof.all_data_bits_proved, proof.all_fanout_covered,
					proof.all_sequential_boundaries_proved, proof.proven_all_obligations,
					v02_reason_code_name(proof.reason_code), proof.usage.visited_cells,
					proof.usage.visited_bits, proof.usage.propagation_events);
				for (const ComparatorProofConclusion &cmp : proof.comparator_cells)
					log("PRODUCTION_GUARD_COMPARATOR cell=%s full_width=%d reduced_width=%d equivalent=%d preserve=%d fanout=%d reason=%s\n",
						cmp.cell_id.c_str(), cmp.full_width, cmp.reduced_width, cmp.equivalent_under_guard,
						cmp.comparator_must_be_preserved, GetSize(cmp.comparator_full_fanout_snapshot),
						v02_reason_code_name(cmp.reason_code));
				for (const GuardFact &guard : proof.current_cycle_guard_facts)
					log("PRODUCTION_GUARD_FACT bit=%s value=%d\n", log_signal(guard.bit), int(guard.value));
				for (const ProductionFoldedDataBitConclusion &fold : proof.folded_data_bits)
					log("PRODUCTION_GUARD_FOLD comparator=%d cell=%s port=%s port_bit=%d source_bit=%d value=%d proved=%d reason=%s\n",
						fold.comparator_index, fold.cell_id.c_str(), fold.port.str().c_str(), fold.port_bit,
						fold.source_bit_index, int(fold.replacement), fold.proved, v02_reason_code_name(fold.reason_code));
				for (const CellSnapshot &cell : proof.affected_cells) log_snapshot("guard-affected", cell);
			}
			for (const ProductionConstantTableProof &proof : bundle.constant_tables) {
				log("PRODUCTION_TABLE_PROOF module=%s address_width=%d output_width=%d rows=%d source_cells=%d sinks=%d status=%d complete=%d rows_proved=%d mapped=%d all=%d reason=%s cells=%d bits=%d events=%d\n",
					proof.module.c_str(), proof.address_width, proof.output_width, GetSize(proof.truth_table),
					GetSize(proof.source_cells), GetSize(proof.sequential_sinks), int(proof.status),
					proof.complete_binary_domain, proof.proven_all_rows, proof.all_output_bits_mapped,
					proof.proven_all_obligations, v02_reason_code_name(proof.reason_code),
					proof.usage.visited_cells, proof.usage.visited_bits, proof.usage.propagation_events);
				for (int row = 0; row < GetSize(proof.truth_table); ++row)
					log("PRODUCTION_TABLE_ROW address=%d bits=%s\n", row, proof.truth_table[row].as_string().c_str());
				for (const ProductionSequentialBitSnapshot &sink : proof.sequential_sinks)
					log("PRODUCTION_TABLE_SINK logical_bit=%d cell=%s q_bit=%d mode_facts=%d\n",
						sink.logical_output_bit, sink.cell_id.c_str(), sink.q_bit, GetSize(sink.table_mode_facts));
				for (const CellSnapshot &cell : proof.source_cells) log_snapshot("table-source", cell);
				std::set<std::string> emitted_sinks;
				for (const ProductionSequentialBitSnapshot &sink : proof.sequential_sinks)
					if (emitted_sinks.insert(sink.cell_id).second) log_snapshot("table-sink", sink.snapshot);
			}
			log("PRODUCTION_GUARD_REPORTS %d\n", GetSize(bundle.guarded_cones));
			log("PRODUCTION_TABLE_REPORTS %d\n", GetSize(bundle.constant_tables));
			log("PRODUCTION_PROOF_NETLIST_UNCHANGED %d\n", before == fingerprint(module, design));
		}
	}
} ProductionCase34ReportPass;

} // namespace
} // namespace PmuxOpt

YOSYS_NAMESPACE_END
