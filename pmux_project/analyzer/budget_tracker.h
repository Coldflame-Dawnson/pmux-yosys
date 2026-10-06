#ifndef PMUX_OPT_BUDGET_TRACKER_H
#define PMUX_OPT_BUDGET_TRACKER_H

#include <cstddef>

namespace PmuxOptTestable {

enum class BudgetKind { None, Cells, Bits, Events };

class BudgetTracker {
public:
	BudgetTracker(std::size_t cells, std::size_t bits, std::size_t events) :
		cell_limit_(cells), bit_limit_(bits), event_limit_(events) { }

	bool take_cell() { return take(cell_count_, cell_limit_, BudgetKind::Cells); }
	bool take_bit() { return take(bit_count_, bit_limit_, BudgetKind::Bits); }
	bool take_event() { return take(event_count_, event_limit_, BudgetKind::Events); }

	std::size_t cells() const { return cell_count_; }
	std::size_t bits() const { return bit_count_; }
	std::size_t events() const { return event_count_; }
	BudgetKind exhausted() const { return exhausted_; }

private:
	bool take(std::size_t &count, std::size_t limit, BudgetKind kind)
	{
		if (exhausted_ != BudgetKind::None)
			return false;
		if (count == limit) {
			exhausted_ = kind;
			return false;
		}
		++count;
		return true;
	}

	std::size_t cell_limit_, bit_limit_, event_limit_;
	std::size_t cell_count_ = 0, bit_count_ = 0, event_count_ = 0;
	BudgetKind exhausted_ = BudgetKind::None;
};

} // namespace PmuxOptTestable

#endif
