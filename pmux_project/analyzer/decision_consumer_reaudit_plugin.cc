#include "../pass/case34_rewrite.cc"

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {
namespace {

std::string reaudit_fingerprint(RTLIL::Module *module)
{
	std::ostringstream stream;
	RTLIL_BACKEND::dump_module(stream, "", module, module->design, false);
	return stream.str();
}

int reaudit_evaluate(const DecisionConeProof &proof, int root, int address)
{
	int node_id = root;
	while (node_id >= 0 && node_id < GetSize(proof.normalized_dag)) {
		const DecisionDagNode &node = proof.normalized_dag[node_id];
		if (node.kind == DecisionDagNodeKind::Constant0) return 0;
		if (node.kind == DecisionDagNodeKind::Constant1) return 1;
		if (node.kind != DecisionDagNodeKind::Test) return -1;
		node_id = (address >> node.input_index) & 1 ? node.high_node : node.low_node;
	}
	return -1;
}

struct DecisionConsumerReauditPass : public Pass {
	DecisionConsumerReauditPass() : Pass("pmux_decision_consumer_reaudit",
			"A-only behavioral reaudit of the decision proof pre-write validator") { }

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		extra_args(args, 1, design);
		int checks = 0, passed = 0;
		for (RTLIL::Module *module : design->selected_modules()) {
			DecisionConeProofRequest request;
			request.module_id = module->name.str();
			request.module = module;
			DecisionConeProof proof = make_decision_cone_analyzer()->analyze(request);
			DecisionPlan baseline_plan;
			Case34RewriteResult baseline_result;
			if (!build_decision_plan(module, proof, baseline_plan, baseline_result))
				continue;

			auto mutation = [&](const char *name, bool expect_accept, auto mutate) {
				DecisionConeProof changed = proof;
				mutate(changed);
				const std::string before = reaudit_fingerprint(module);
				DecisionPlan plan;
				Case34RewriteResult result;
				const bool accepted = build_decision_plan(module, changed, plan, result);
				const bool unchanged = before == reaudit_fingerprint(module);
				const bool ok = accepted == expect_accept && unchanged;
				checks++;
				passed += ok;
				log("REAUDIT_MUTATION name=%s expected=%s actual=%s unchanged=%d result=%s\n",
						name, expect_accept ? "accept" : "reject", accepted ? "accept" : "reject",
						unchanged, ok ? "PASS" : "FAIL");
			};

			mutation("care_long", false, [](auto &p) {
				p.affected_sinks[0].care_mask_lsb_address.bits().push_back(RTLIL::State::S0);
			});
			mutation("value_long", false, [](auto &p) {
				p.affected_sinks[0].value_mask_lsb_address.bits().push_back(RTLIL::State::S0);
			});
			mutation("care_zero_length", false, [](auto &p) {
				p.affected_sinks[0].care_mask_lsb_address.bits().clear();
			});
			mutation("value_zero_length", false, [](auto &p) {
				p.affected_sinks[0].value_mask_lsb_address.bits().clear();
			});
			mutation("support_alias", false, [](auto &p) {
				p.support_lsb_first[1].bit = p.support_lsb_first[0].bit;
			});
			mutation("support_reorder", false, [](auto &p) {
				std::swap(p.support_lsb_first[0].bit, p.support_lsb_first[1].bit);
			});
			{
				DecisionConeProof reordered = proof;
				std::swap(reordered.support_lsb_first[0].bit, reordered.support_lsb_first[1].bit);
				bool found = false;
				int found_sink = -1, found_address = -1, expected = -1, actual = -1;
				for (int sink_index = 0; sink_index < GetSize(reordered.affected_sinks) && !found; ++sink_index) {
					const DecisionSinkProof &sink = reordered.affected_sinks[sink_index];
					for (int address = 0; address < GetSize(sink.care_mask_lsb_address); ++address) {
						if (sink.care_mask_lsb_address[address] != RTLIL::State::S1) continue;
						int permuted = address;
						const int bit0 = address & 1, bit1 = (address >> 1) & 1;
						permuted = (permuted & ~3) | bit1 | (bit0 << 1);
						int got = reaudit_evaluate(reordered, sink.normalized_root_node, permuted);
						int want = sink.value_mask_lsb_address[address] == RTLIL::State::S1;
						if (got != want) {
							found = true; found_sink = sink_index; found_address = address;
							expected = want; actual = got; break;
						}
					}
				}
				log("REAUDIT_COUNTEREXAMPLE name=support_reorder sink=%d address=%d expected=%d actual=%d found=%d\n",
						found_sink, found_address, expected, actual, found);
			}
			mutation("relation_snapshot", false, [](auto &p) {
				p.decision_relations[0].snapshot.type = ID($not);
			});
			mutation("relation_selects", false, [](auto &p) {
				p.decision_relations[0].selects_in_branch_order.clear();
			});
			mutation("relation_kind", false, [](auto &p) {
				for (auto &relation : p.decision_relations)
					if (relation.kind == DecisionRelationKind::MuxPriority) {
						relation.kind = DecisionRelationKind::PmuxMutuallyExclusive;
						return;
					}
			});
			mutation("duplicate_comparator", false, [](auto &p) {
				p.comparators.push_back(p.comparators[0]);
			});

			{
				RTLIL::Cell *comparator = module->cell(RTLIL::IdString(proof.comparators[0].cell_id));
				RTLIL::Wire *extra_wire = module->addWire(NEW_ID);
				RTLIL::Cell *extra = module->addNot(NEW_ID, comparator->getPort(ID::Y), extra_wire);
				const std::string before = reaudit_fingerprint(module);
				DecisionPlan plan;
				Case34RewriteResult result;
				const bool accepted = build_decision_plan(module, proof, plan, result);
				const bool unchanged = before == reaudit_fingerprint(module);
				const bool ok = !accepted && unchanged;
				checks++;
				passed += ok;
				log("REAUDIT_MUTATION name=extra_fanout expected=reject actual=%s unchanged=%d result=%s\n",
						accepted ? "accept" : "reject", unchanged, ok ? "PASS" : "FAIL");
				module->remove(extra);
				pool<RTLIL::Wire *> extra_wires;
				extra_wires.insert(extra_wire);
				module->remove(extra_wires);
			}

			{
				DecisionPlan plan;
				Case34RewriteResult result;
				bool built = build_decision_plan(module, proof, plan, result);
				const DecisionSequentialBitSnapshot &boundary = proof.affected_sinks[0].sequential_snapshot;
				RTLIL::Cell *cell = module->cell(RTLIL::IdString(boundary.cell_id));
				auto old = cell->attributes.find(ID::keep);
				const bool had_keep = old != cell->attributes.end();
				RTLIL::Const old_keep = had_keep ? old->second : RTLIL::Const();
				cell->attributes[ID::keep] = RTLIL::Const(1, 1);
				const std::string before = reaudit_fingerprint(module);
				const bool accepted = built && precheck(plan);
				const bool unchanged = before == reaudit_fingerprint(module);
				const bool ok = built && !accepted && unchanged;
				checks++;
				passed += ok;
				log("REAUDIT_MUTATION name=second_snapshot_change expected=reject actual=%s unchanged=%d result=%s\n",
						accepted ? "accept" : "reject", unchanged, ok ? "PASS" : "FAIL");
				if (had_keep) cell->attributes[ID::keep] = old_keep;
				else cell->attributes.erase(ID::keep);
			}

			{
				RTLIL::Cell *cell = module->cell(RTLIL::IdString(proof.comparators[0].cell_id));
				RTLIL::SigSpec old_a = cell->getPort(ID::A), old_b = cell->getPort(ID::B);
				RTLIL::Const old_as = cell->getParam(ID::A_SIGNED), old_bs = cell->getParam(ID::B_SIGNED);
				cell->setPort(ID::A, old_b);
				cell->setPort(ID::B, old_a);
				cell->setParam(ID::A_SIGNED, old_bs);
				cell->setParam(ID::B_SIGNED, old_as);
				DecisionConeProofRequest swapped_request;
				swapped_request.module_id = module->name.str();
				swapped_request.module = module;
				DecisionConeProof swapped = make_decision_cone_analyzer()->analyze(swapped_request);
				const std::string before = reaudit_fingerprint(module);
				DecisionPlan plan;
				Case34RewriteResult result;
				const bool accepted = build_decision_plan(module, swapped, plan, result);
				const bool unchanged = before == reaudit_fingerprint(module);
				const bool ok = accepted && unchanged;
				checks++;
				passed += ok;
				log("REAUDIT_MUTATION name=constant_side_swap expected=accept actual=%s unchanged=%d result=%s\n",
						accepted ? "accept" : "reject", unchanged, ok ? "PASS" : "FAIL");
				cell->setPort(ID::A, old_a);
				cell->setPort(ID::B, old_b);
				cell->setParam(ID::A_SIGNED, old_as);
				cell->setParam(ID::B_SIGNED, old_bs);
			}

			log("REAUDIT_SUMMARY passed=%d checks=%d failures=%d\n", passed, checks, checks - passed);
			return;
		}
		log_cmd_error("pmux_decision_consumer_reaudit: no valid decision cone found.\n");
	}
} DecisionConsumerReauditPass;

} // namespace
} // namespace PmuxOpt

YOSYS_NAMESPACE_END
