/**
 * @file
 * @brief A small discriminated media result for toolchains without std::expected.
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

#include <media/error.hpp>

#include <optional>
#include <utility>
#include <variant>

namespace wiola::media {

struct unexpected {
    Error error;
};

template<typename T>
class Result {
public:
    Result(T value)
        : storage_{std::move(value)}
    {
    }

    Result(unexpected error)
        : storage_{std::move(error.error)}
    {
    }

    [[nodiscard]] bool has_value() const noexcept { return std::holds_alternative<T>(storage_); }

    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

    T& value() & { return std::get<T>(storage_); }

    const T& value() const & { return std::get<T>(storage_); }

    T&& value() && { return std::get<T>(std::move(storage_)); }

    T& operator*() & { return value(); }

    const T& operator*() const & { return value(); }

    T* operator->() { return &value(); }

    const T* operator->() const { return &value(); }

    Error& error() { return std::get<Error>(storage_); }

    const Error& error() const { return std::get<Error>(storage_); }

private:
    std::variant<T, Error> storage_;
};

template<>
class Result<void> {
public:
    Result() = default;

    Result(unexpected error)
        : error_{std::move(error.error)}
    {
    }

    [[nodiscard]] bool has_value() const noexcept { return !error_.has_value(); }

    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

    void value() const
    {
        if (error_)
            throw std::bad_variant_access{};
    }

    Error& error() { return *error_; }

    const Error& error() const { return *error_; }

private:
    std::optional<Error> error_;
};

} // namespace wiola::media
