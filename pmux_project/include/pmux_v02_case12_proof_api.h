#ifndef PMUX_V02_CASE12_PROOF_API_H
#define PMUX_V02_CASE12_PROOF_API_H

#include "pmux_v02_case34_proof_api.h"

#include <memory>
#include <map>
#include <string>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {

struct Case12AttributeSnapshot {
	RTLIL::IdString attribute;
	RTLIL::Const value;
};

struct Case12CellSnapshot {
	std::string cell_id;
	RTLIL::IdString type;
	std::vector<CellPortSnapshot> ports;
	std::vector<CellParameterSnapshot> parameters;
	std::vector<Case12AttributeSnapshot> attributes;
};

struct PairedPmuxProof {
	std::string module;
	Case12CellSnapshot first;
	Case12CellSnapshot second;
	RTLIL::SigSpec state_lsb_first;
	RTLIL::SigSpec select;
	RTLIL::SigSpec first_a, second_a;
	RTLIL::SigSpec first_y, second_y;
	std::vector<RTLIL::SigSpec> first_data_by_code;
	std::vector<RTLIL::SigSpec> second_data_by_code;
	int width = 0;
	int state_width = 0;
	bool complete_decode = false;
	bool cross_relation_proved = false;
	bool proven_all_obligations = false;
	std::string reason;
};

struct PriorityMuxProof {
	std::string module;
	std::vector<Case12CellSnapshot> cells_outer_to_inner;
	std::vector<RTLIL::SigBit> select_outer_to_inner;
	std::vector<RTLIL::SigSpec> data_outer_to_inner;
	RTLIL::SigSpec output;
	int width = 0;
	bool zero_default = false;
	bool exclusive_intermediate_fanout = false;
	bool proven_all_obligations = false;
	std::string reason;
};

struct Case12ProofBundle {
	std::vector<PairedPmuxProof> paired_pmux;
	std::vector<PriorityMuxProof> priority_mux4;
	std::map<std::string, int> reason_counts;
};

class Case12Analyzer {
public:
	virtual ~Case12Analyzer() = default;
	virtual Case12ProofBundle analyze(RTLIL::Module *module) const = 0;
};

std::unique_ptr<Case12Analyzer> make_case12_analyzer();

} // namespace PmuxOpt

YOSYS_NAMESPACE_END

#endif
