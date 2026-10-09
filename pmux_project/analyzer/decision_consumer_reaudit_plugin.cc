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

bool ordered_removed_index_fixture(int removed_bit, bool reorder_support, bool two_cells)
{
	RTLIL::Design scratch;
	RTLIL::Module *module = scratch.addModule(ID(ordered_fixture));
	RTLIL::Wire *source = module->addWire(ID(source), 4);
	RTLIL::Wire *output0 = module->addWire(ID(output0));
	auto add_comparator = [&](RTLIL::IdString name, RTLIL::Wire *output) {
		RTLIL::Cell *cell = module->addCell(name, ID($eq));
		cell->setPort(ID::A, RTLIL::SigSpec(source));
		cell->setPort(ID::B, RTLIL::Const(10, 4));
		cell->setPort(ID::Y, RTLIL::SigSpec(output));
		cell->setParam(ID::A_WIDTH, 4);
		cell->setParam(ID::B_WIDTH, 4);
		cell->setParam(ID::Y_WIDTH, 1);
		cell->setParam(ID::A_SIGNED, 0);
		cell->setParam(ID::B_SIGNED, 0);
		return cell;
	};
	RTLIL::Cell *first = add_comparator(ID(cmp0), output0);
	DecisionConeProof proof;
	proof.source_value = RTLIL::SigSpec(source);
	int input_index = 0;
	for (int source_bit = 0; source_bit < 4; ++source_bit)
		if (source_bit != removed_bit) {
			DecisionSupportBit support;
			support.input_index = input_index++;
			support.bit = RTLIL::SigBit(source, source_bit);
			proof.support_lsb_first.push_back(support);
		}
	if (reorder_support)
		std::swap(proof.support_lsb_first[0].bit, proof.support_lsb_first[1].bit);
	auto conclusion = [&](RTLIL::Cell *cell) {
		ComparatorProofConclusion item;
		item.cell_id = cell->name.str();
		item.comparator_constant = RTLIL::Const(10, 4);
		item.full_width = 4;
		item.reduced_width = 3;
		item.removed_bit_indices = {removed_bit};
		item.equivalent_under_guard = true;
		item.comparator_must_be_preserved = false;
		item.reason_code = V02ReasonCode::Proved;
		return item;
	};
	if (!validate_comparator(module, proof, conclusion(first))) return false;
	if (!two_cells) return true;
	RTLIL::Wire *output1 = module->addWire(ID(output1));
	RTLIL::Cell *second = add_comparator(ID(cmp1), output1);
	return validate_comparator(module, proof, conclusion(second));
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

			int ordered_checks = 0, ordered_passed = 0;
			auto ordered_reject = [&](const char *name, auto mutate) {
				DecisionConeProof changed = proof;
				mutate(changed);
				const std::string before = reaudit_fingerprint(module);
				DecisionPlan plan;
				Case34RewriteResult result;
				const bool accepted = build_decision_plan(module, changed, plan, result);
				const bool unchanged = before == reaudit_fingerprint(module);
				const bool ok = !accepted && unchanged;
				ordered_checks++;
				ordered_passed += ok;
				log("ORDERED_REAUDIT name=%s expected=reject actual=%s unchanged=%d result=%s\n",
						name, accepted ? "accept" : "reject", unchanged, ok ? "PASS" : "FAIL");
			};
			ordered_reject("relation_delete_mux", [](auto &p) {
				for (auto it = p.decision_relations.begin(); it != p.decision_relations.end(); ++it)
					if (it->kind != DecisionRelationKind::SequentialControlPriority) {
						p.decision_relations.erase(it);
						return;
					}
			});
			ordered_reject("relation_duplicate_mux", [](auto &p) {
				for (const auto &relation : p.decision_relations)
					if (relation.kind != DecisionRelationKind::SequentialControlPriority) {
						p.decision_relations.push_back(relation);
						return;
					}
			});
			ordered_reject("relation_replace_mux", [](auto &p) {
				int first = -1, second = -1;
				for (int i = 0; i < GetSize(p.decision_relations); ++i)
					if (p.decision_relations[i].kind != DecisionRelationKind::SequentialControlPriority) {
						if (first < 0) first = i; else { second = i; break; }
					}
				if (first >= 0 && second >= 0)
					p.decision_relations[first] = p.decision_relations[second];
			});
			ordered_reject("relation_delete_sequential", [](auto &p) {
				for (auto it = p.decision_relations.begin(); it != p.decision_relations.end(); ++it)
					if (it->kind == DecisionRelationKind::SequentialControlPriority) {
						p.decision_relations.erase(it);
						return;
					}
			});
			ordered_reject("relation_duplicate_sequential", [](auto &p) {
				for (const auto &relation : p.decision_relations)
					if (relation.kind == DecisionRelationKind::SequentialControlPriority) {
						p.decision_relations.push_back(relation);
						return;
					}
			});
			if (!proof.unaffected_sequential_bits.empty())
				ordered_reject("relation_cross_unaffected", [&](auto &p) {
					const auto &replacement = p.unaffected_sequential_bits.front();
					for (auto &relation : p.decision_relations)
						if (relation.kind == DecisionRelationKind::SequentialControlPriority) {
							relation.cell_id = replacement.cell_id;
							relation.snapshot = replacement.cell_snapshot;
							relation.selects_in_branch_order.clear();
							RTLIL::Cell *cell = module->cell(RTLIL::IdString(replacement.cell_id));
							for (RTLIL::IdString port : {ID::ARST, ID::EN, ID::SRST})
								if (cell->hasPort(port))
									relation.selects_in_branch_order.push_back(cell->getPort(port)[0]);
							return;
						}
				});
			std::map<std::string, int> sink_multiplicity;
			for (const auto &sink : proof.affected_sinks)
				sink_multiplicity[sink.sequential_snapshot.cell_id]++;
			const bool same_cell_multibit = std::any_of(sink_multiplicity.begin(), sink_multiplicity.end(),
					[](const auto &item) { return item.second > 1; });
			ordered_checks++;
			ordered_passed += same_cell_multibit;
			log("ORDERED_REAUDIT name=same_cell_multibit_observed expected=accept actual=%s result=%s\n",
					same_cell_multibit ? "accept" : "missing", same_cell_multibit ? "PASS" : "FAIL");
			for (int removed_bit : {0, 2, 3}) {
				const bool ok = ordered_removed_index_fixture(removed_bit, false, false);
				ordered_checks++;
				ordered_passed += ok;
				log("ORDERED_REAUDIT name=removed_index_%d expected=accept actual=%s result=%s\n",
						removed_bit, ok ? "accept" : "reject", ok ? "PASS" : "FAIL");
			}
			{
				const bool rejected = !ordered_removed_index_fixture(1, true, false);
				ordered_checks++; ordered_passed += rejected;
				log("ORDERED_REAUDIT name=synthetic_support_reorder expected=reject actual=%s result=%s\n",
						rejected ? "reject" : "accept", rejected ? "PASS" : "FAIL");
			}
			{
				const bool ok = ordered_removed_index_fixture(3, false, true);
				ordered_checks++; ordered_passed += ok;
				log("ORDERED_REAUDIT name=distinct_comparators_same_truth expected=accept actual=%s result=%s\n",
						ok ? "accept" : "reject", ok ? "PASS" : "FAIL");
			}
			log("ORDERED_REAUDIT_SUMMARY passed=%d checks=%d failures=%d\n",
					ordered_passed, ordered_checks, ordered_checks - ordered_passed);
			return;
		}
		log_cmd_error("pmux_decision_consumer_reaudit: no valid decision cone found.\n");
	}
} DecisionConsumerReauditPass;

} // namespace
} // namespace PmuxOpt

YOSYS_NAMESPACE_END
