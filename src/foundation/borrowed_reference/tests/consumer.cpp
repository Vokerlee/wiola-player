#ifdef WIOLA_PRIVATE_HEADER
#include WIOLA_PRIVATE_HEADER
#endif

#include <wiola/foundation/borrowed.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<wiola::core::Borrowed<int>>);
static_assert(std::is_nothrow_move_constructible_v<wiola::core::Borrowed<int>>);

int read(wiola::core::Borrowed<int> value)
{
    return *value;
}
