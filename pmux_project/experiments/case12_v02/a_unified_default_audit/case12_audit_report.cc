#include "kernel/yosys.h"
#include "pmux_v02_case12_proof_api.h"

USING_YOSYS_NAMESPACE

PRIVATE_NAMESPACE_BEGIN

struct Case12AuditReportPass : public Pass {
	Case12AuditReportPass() : Pass("case12_audit_report", "A-only case12 proof report") { }
	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		extra_args(args, 1, design);
		for (RTLIL::Module *module : design->selected_modules()) {
			auto bundle = PmuxOpt::make_case12_analyzer()->analyze(module);
			log("CASE12_AUDIT module=%s paired=%d priority=%d\n", log_id(module),
					GetSize(bundle.paired_pmux), GetSize(bundle.priority_mux4));
			for (const auto &proof : bundle.paired_pmux)
				log("CASE12_AUDIT_PAIRED module=%s first=%s second=%s all=%d reason=%s\n",
						log_id(module), proof.first.cell_id.c_str(), proof.second.cell_id.c_str(),
						proof.proven_all_obligations, proof.reason.c_str());
		}
	}
} Case12AuditReportPass;

PRIVATE_NAMESPACE_END
