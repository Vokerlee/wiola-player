/**
 * @file
 * @brief Shared callable identity retained through reentrant invocation.
 * @author Roman Glaz
 * @copyright (c) 2026, <vokerlee@gmail.com>
 *
 * Wiola is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Wiola is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wiola. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <functional>
#include <memory>
#include <utility>

namespace wiola::core {

template<typename Signature>
class SharedCallback;

/// Copies share mutable callable state. An invocation retains its target even if that target
/// replaces or destroys this container. Captured references and caller continuations keep their
/// own lifetime requirements; this value does not synchronize concurrent invocations.
template<typename Result, typename... Arguments>
class SharedCallback<Result(Arguments...)> final {
    using Function = std::function<Result(Arguments...)>;

public:
    SharedCallback() noexcept = default;

    explicit SharedCallback(Function function)
    {
        if (function)
            function_ = std::make_shared<Function>(std::move(function));
    }

    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(function_); }

    Result operator()(Arguments... arguments) const
    {
        const auto active = function_;

        if (!active)
            throw std::bad_function_call{};

        return (*active)(std::forward<Arguments>(arguments)...);
    }

private:
    std::shared_ptr<Function> function_;
};

} // namespace wiola::core
