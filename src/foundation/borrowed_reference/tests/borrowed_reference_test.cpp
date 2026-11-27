#include <wiola/foundation/borrowed.hpp>

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<wiola::core::Borrowed<int>>);
static_assert(!std::is_copy_assignable_v<wiola::core::Borrowed<int>>);
static_assert(std::is_nothrow_move_constructible_v<wiola::core::Borrowed<int>>);
static_assert(std::is_nothrow_move_assignable_v<wiola::core::Borrowed<int>>);

int main()
{
    int first = 3;
    int second = 9;
    wiola::core::Borrowed<int> empty;

    if (empty) {
        return 1;
    }

    wiola::core::Borrowed<int> source{first};
    wiola::core::Borrowed<int> destination{std::move(source)};

    if (source || !destination || destination.operator->() != &first) {
        return 2;
    }

    wiola::core::Borrowed<int> replacement{second};
    replacement = std::move(destination);

    if (destination || *replacement != first) {
        return 3;
    }

    *replacement = 17;

    if (first != 17 || second != 9) {
        return 4;
    }

    destination = std::move(empty);

    return destination ? 5 : 0;
}
