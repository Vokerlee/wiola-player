/**
 * @file
 * @brief Typed ownership nodes and retained dependency edges.
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

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace wiola::core {

namespace detail {

template<typename Pointer, typename Target>
inline constexpr bool owns_compatible_object = false;

template<typename Object, typename Deleter, typename Target>
inline constexpr bool owns_compatible_object<std::unique_ptr<Object, Deleter>, Target> =
    !std::is_array_v<Object> &&
    std::convertible_to<typename std::unique_ptr<Object, Deleter>::pointer, Target*>;

} // namespace detail

template<typename T>
class WeakLease;

/// A non-null ownership node. Copies, including copies from rvalues, retain the same node.
/// A dependent node owns its dependency edges until after its own object is destroyed.
template<typename T>
class Lease final {
    template<typename>
    friend class Lease;
    friend class WeakLease<T>;

public:
    Lease(const Lease&) noexcept = default;
    Lease& operator=(const Lease&) noexcept = default;

    template<typename U>
        requires(!std::same_as<T, U> && std::convertible_to<U*, T*>)
    Lease(const Lease<U>& other) noexcept
        : object_{other.object_}
    {
    }

    template<typename... Arguments>
        requires(!std::is_array_v<T> && std::constructible_from<T, Arguments...>)
    [[nodiscard]] static Lease make(Arguments&&... arguments)
    {
        return Lease{std::make_shared<T>(std::forward<Arguments>(arguments)...)};
    }

    template<typename Factory, typename... Dependencies>
        requires std::invocable<Factory, Dependencies&...> &&
        detail::owns_compatible_object<std::invoke_result_t<Factory, Dependencies&...>, T>
    [[nodiscard]] static Lease dependent(Factory&& factory,
        const Lease<Dependencies>&... dependencies)
    {
        auto anchors = std::tuple{dependencies...};
        auto object = std::apply(
            [&](const auto&... dependency) {
                return std::invoke(std::forward<Factory>(factory), dependency.get()...);
            },
            anchors
        );
        if (!object)
            throw std::invalid_argument{"a lifetime node needs an owned object"};

        struct Node {
            decltype(anchors) dependencies;
            decltype(object) value;
        };

        auto node = std::make_shared<Node>(std::move(anchors), std::move(object));
        T* pointer = node->value.get();
        return Lease{
            std::shared_ptr<T>{std::move(node), pointer}
        };
    }

    /// A direct data member is part of its owning node; projection retains that entire node.
    template<auto Member>
        requires std::is_member_object_pointer_v<decltype(Member)> &&
        std::invocable<decltype(Member), T&>
    [[nodiscard]] auto member() const
    {
        using Value = std::remove_reference_t<std::invoke_result_t<decltype(Member), T&>>;
        auto* value = std::addressof(std::invoke(Member, *object_));
        return Lease<Value>{
            std::shared_ptr<Value>{object_, value}
        };
    }

    [[nodiscard]] T& get() const & noexcept { return *object_; }

    T& get() const && = delete;

    [[nodiscard]] T* operator->() const & noexcept { return object_.get(); }

    T* operator->() const && = delete;

    [[nodiscard]] T& operator*() const & noexcept { return *object_; }

    T& operator*() const && = delete;

    [[nodiscard]] WeakLease<T> weak() const noexcept;

private:
    explicit Lease(std::shared_ptr<T> object) noexcept
        : object_{std::move(object)}
    {
    }

    std::shared_ptr<T> object_;
};

/// Observation acquires a real ownership lease before exposing an object.
template<typename T>
class WeakLease final {
    friend class Lease<T>;

public:
    WeakLease() noexcept = default;

    [[nodiscard]] std::optional<Lease<T>> lock() const noexcept
    {
        if (auto object = object_.lock())
            return Lease<T>{std::move(object)};
        return std::nullopt;
    }

private:
    explicit WeakLease(const std::shared_ptr<T>& object) noexcept
        : object_{object}
    {
    }

    std::weak_ptr<T> object_;
};

template<typename T>
WeakLease<T> Lease<T>::weak() const noexcept
{
    return WeakLease<T>{object_};
}

} // namespace wiola::core
