#ifndef PMUX_OPT_READONLY_ANALYZER_H
#define PMUX_OPT_READONLY_ANALYZER_H

#include "pmux_opt_api.h"

#include <memory>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {

std::unique_ptr<EntryAnalyzer> make_readonly_entry_analyzer();

} // namespace PmuxOpt

YOSYS_NAMESPACE_END

#endif
