// SPDX-License-Identifier: GPL-3.0-or-later

#include <wiola/units.hpp>

#include <chrono>
using namespace wiola::units;
static_assert(UnitOf<Ms, Time>);
static_assert(!UnitOf<Hz, Time>);
static_assert(Time{std::chrono::milliseconds{1000}}.seconds() == 1.0);

int main()
{
    Frequency rate{2};
    return rate.to<Time>().seconds() == 0.5 && (rate * Time{3}) == 6.0 ? 0 : 1;
}
