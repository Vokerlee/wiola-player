// SPDX-License-Identifier: GPL-3.0-or-later

#include <wiola/foundation/borrowed.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<wiola::core::Borrowed<int>>);
static_assert(std::is_nothrow_move_constructible_v<wiola::core::Borrowed<int>>);

int read(wiola::core::Borrowed<int> value)
{
    return *value;
}

int main()
{
    int value = 7;
    return read(wiola::core::Borrowed<int>{value}) == 7 ? 0 : 1;
}
