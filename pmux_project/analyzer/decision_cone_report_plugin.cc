#include "pmux_v02_decision_cone_proof_api.h"

#include "backends/rtlil/rtlil_backend.h"
#include "kernel/log.h"
#include "kernel/register.h"

#include <map>
#include <sstream>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {
namespace {

std::string fingerprint(RTLIL::Module *module, RTLIL::Design *design)
{
	std::ostringstream out; RTLIL_BACKEND::dump_module(out, "", module, design, false); return out.str();
}

void log_cell(const char *scope, const CellSnapshot &snapshot)
{
	log("DECISION_CONE_CELL scope=%s cell=%s type=%s ports=%d params=%d\n", scope,
		snapshot.cell_id.c_str(), snapshot.type.str().c_str(), GetSize(snapshot.ports), GetSize(snapshot.parameters));
	for (const auto &port : snapshot.ports)
		log("DECISION_CONE_PORT scope=%s cell=%s port=%s signal=%s\n", scope, snapshot.cell_id.c_str(),
			port.port.str().c_str(), log_signal(port.signal));
	for (const auto &param : snapshot.parameters)
		log("DECISION_CONE_PARAM scope=%s cell=%s param=%s value=%s\n", scope, snapshot.cell_id.c_str(),
			param.parameter.str().c_str(), log_signal(RTLIL::SigSpec(param.value)));
}

void log_boundary(const char *scope, const DecisionSequentialBitSnapshot &snapshot)
{
	log("DECISION_CONE_BOUNDARY scope=%s logical=%d cell=%s d_bit=%d q_bit=%d port_id=%d port_input=%d port_output=%d cell_attrs=%d q_attrs=%d proof_id=%s\n",
		scope, snapshot.logical_sink_bit, snapshot.cell_id.c_str(), snapshot.d_bit, snapshot.q_bit,
		snapshot.q_wire_port_id, snapshot.q_wire_port_input, snapshot.q_wire_port_output,
		GetSize(snapshot.cell_attributes), GetSize(snapshot.q_wire_attributes), snapshot.proof_id.c_str());
	for (const auto &attribute : snapshot.cell_attributes)
		log("DECISION_CONE_ATTRIBUTE scope=%s owner=cell cell=%s attr=%s value=%s\n", scope, snapshot.cell_id.c_str(),
			attribute.attribute.str().c_str(), log_signal(RTLIL::SigSpec(attribute.value)));
	for (const auto &attribute : snapshot.q_wire_attributes)
		log("DECISION_CONE_ATTRIBUTE scope=%s owner=q_wire cell=%s attr=%s value=%s\n", scope, snapshot.cell_id.c_str(),
			attribute.attribute.str().c_str(), log_signal(RTLIL::SigSpec(attribute.value)));
}

int evaluate_dag(const DecisionConeProof &proof, int root, int address)
{
	int node = root;
	while (node >= 0 && node < GetSize(proof.normalized_dag) && proof.normalized_dag[node].kind == DecisionDagNodeKind::Test) {
		const DecisionDagNode &item = proof.normalized_dag[node];
		if (item.low_node >= item.node_id || item.high_node >= item.node_id) return -1;
		node = (address >> item.input_index) & 1 ? item.high_node : item.low_node;
	}
	return node == 0 ? 0 : node == 1 ? 1 : -1;
}

struct DecisionConeReportPass : public Pass {
	DecisionConeReportPass() : Pass("pmux_case3_decision_cone_proof_report",
		"emit the read-only normalized next-state decision-cone proof") { }
	void help() override
	{
		log("    pmux_case3_decision_cone_proof_report\n\n");
		log("Prove and serialize complete guarded D-input decision cones without modifying RTLIL.\n");
	}
	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		log_header(design, "Executing PMUX case3 decision-cone proof report.\n"); extra_args(args, 1, design);
		for (RTLIL::Module *module : design->selected_modules()) {
			std::string before = fingerprint(module, design);
			DecisionConeProofRequest request; request.module = module; request.module_id = module->name.str();
			DecisionConeProof proof = make_decision_cone_analyzer()->analyze(request);
			log("DECISION_CONE_PROOF module=%s source_width=%d support=%d fixed=%d comparators=%d relations=%d dag_nodes=%d sinks=%d unaffected=%d source_cells=%d external=%d status=%d structure=%d guard=%d selection=%d inside=%d outside=%d sink_bits=%d fanout=%d boundaries=%d dag=%d all=%d reason=%s cells=%d bits=%d events=%d\n",
				proof.module.c_str(), GetSize(proof.source_value), GetSize(proof.support_lsb_first), GetSize(proof.guard_fixed_source_bits),
				GetSize(proof.comparators), GetSize(proof.decision_relations), GetSize(proof.normalized_dag),
				GetSize(proof.affected_sinks), GetSize(proof.unaffected_sequential_bits), GetSize(proof.source_cells),
				GetSize(proof.external_consumers), int(proof.status), proof.structure_complete, proof.current_cycle_guard_proved,
				proof.selection_semantics_proved, proof.guard_inside_equivalence_proved, proof.guard_outside_equivalence_proved,
				proof.all_sink_bits_proved, proof.all_fanout_covered, proof.all_sequential_boundaries_proved,
				proof.normalized_dag_complete, proof.proven_all_obligations, decision_cone_reason_code_name(proof.reason_code),
				proof.usage.visited_cells, proof.usage.visited_bits, proof.usage.propagation_events);
			for (const GuardFact &fact : proof.outer_guard_facts)
				log("DECISION_CONE_GUARD bit=%s value=%d\n", log_signal(fact.bit), int(fact.value));
			for (const DecisionSupportBit &support : proof.support_lsb_first)
				log("DECISION_CONE_SUPPORT index=%d bit=%s module_input=%d sequential_q=%d cell=%s q_bit=%d proof_id=%s\n",
					support.input_index, log_signal(support.bit), support.module_input, support.sequential_q,
					support.sequential_cell_id.c_str(), support.sequential_q_bit, support.proof_id.c_str());
			for (const DecisionDagNode &node : proof.normalized_dag)
				log("DECISION_CONE_NODE id=%d kind=%d input=%d low=%d high=%d proof_id=%s\n",
					node.node_id, int(node.kind), node.input_index, node.low_node, node.high_node, node.proof_id.c_str());
			for (const DecisionSinkProof &sink : proof.affected_sinks) {
				log("DECISION_CONE_SINK logical=%d cell=%s d_bit=%d q_bit=%d root=%d care=%s value=%s eq=%d outside=%d default=%d hold=%d reset=%d enable=%d init=%d no_cross=%d fanout=%d reason=%s proof_id=%s\n",
					sink.logical_sink_bit, sink.cell_id.c_str(), sink.d_bit, sink.q_bit, sink.normalized_root_node,
					sink.care_mask_lsb_address.as_string().c_str(), sink.value_mask_lsb_address.as_string().c_str(),
					sink.equivalent_on_all_care_rows, sink.guard_outside_behavior_preserved, sink.default_path_preserved,
					sink.hold_path_preserved, sink.reset_path_preserved, sink.enable_path_preserved,
					sink.initialization_preserved, sink.no_cross_dff_propagation, sink.all_fanout_covered,
					decision_cone_reason_code_name(sink.reason_code), sink.proof_id.c_str());
				log_cell("sink", sink.sequential_snapshot.cell_snapshot);
				log_boundary("sink", sink.sequential_snapshot);
			}
			for (const DecisionSequentialBitSnapshot &sink : proof.unaffected_sequential_bits) {
				log_boundary("unaffected", sink); log_cell("unaffected", sink.cell_snapshot);
			}
			for (const DecisionRelationProof &relation : proof.decision_relations)
				log("DECISION_CONE_RELATION kind=%d cell=%s selects=%d disjoint=%d overlap=%d last=%d reason=%s proof_id=%s\n",
					int(relation.kind), relation.cell_id.c_str(), GetSize(relation.selects_in_branch_order),
					relation.pairwise_disjoint, relation.overlap_checked, relation.last_assignment_semantics_proved,
					decision_cone_reason_code_name(relation.reason_code), relation.proof_id.c_str());
			for (const CellSnapshot &cell : proof.source_cells) log_cell("source", cell);
			for (const CellSnapshot &cell : proof.external_consumers) log_cell("external", cell);
			bool reconstructed = proof.proven_all_obligations;
			int constant_roots = 0, shared_root_groups = 0;
			std::map<int, int> root_counts;
			for (const DecisionSinkProof &sink : proof.affected_sinks) {
				constant_roots += sink.normalized_root_node == 0 || sink.normalized_root_node == 1;
				root_counts[sink.normalized_root_node]++;
				for (int row = 0; row < GetSize(sink.care_mask_lsb_address); ++row)
					if (sink.care_mask_lsb_address[row] == RTLIL::State::S1 &&
							evaluate_dag(proof, sink.normalized_root_node, row) != (sink.value_mask_lsb_address[row] == RTLIL::State::S1))
						reconstructed = false;
			}
			for (const auto &item : root_counts) if (item.second > 1 && item.first > 1) shared_root_groups++;
			log("DECISION_CONE_TEST_CONSUMER reconstructed=%d constant_roots=%d shared_root_groups=%d\n",
				reconstructed, constant_roots, shared_root_groups);
			log("DECISION_CONE_NETLIST_UNCHANGED %d\n", before == fingerprint(module, design));
		}
	}
} DecisionConeReportPass;

} // namespace
} // namespace PmuxOpt

YOSYS_NAMESPACE_END
