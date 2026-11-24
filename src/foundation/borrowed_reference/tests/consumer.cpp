// SPDX-License-Identifier: GPL-3.0-or-later

#include <wiola/foundation/borrowed.hpp>
#include <wiola/foundation/handle.hpp>

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<wiola::core::Borrowed<int>>);
static_assert(std::is_nothrow_move_constructible_v<wiola::core::Borrowed<int>>);

class IntHandle final : public wiola::core::Handle<int> {
public:
    explicit IntHandle(int& value)
        : Handle{value}
    {
    }

    int read() const { return *owner(); }
};

int read(wiola::core::Borrowed<int> value)
{
    return *value;
}

int main()
{
    int value = 7;
    IntHandle first{value};
    IntHandle moved{std::move(first)};
    if (first || !moved || moved.read() != value)
        return 1;
    wiola::core::Claimable capability;
    if (!capability.claim() || capability.claim())
        return 2;
    capability.release();
    return capability.claim() && read(wiola::core::Borrowed<int>{value}) == value ? 0 : 3;
}
