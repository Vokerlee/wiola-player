/**
 * @file
 * @brief Typed identities and capabilities for live player documents.
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
#include <initializer_list>
#include <library/identity.hpp>
#include <utility>
#include <variant>

namespace wiola::gui {

struct QueueDocumentId {
    auto operator<=>(const QueueDocumentId&) const = default;
};

class DocumentId final {
public:
    static DocumentId queue() { return DocumentId{QueueDocumentId{}}; }

    static DocumentId playlist(library::PlaylistId id) { return DocumentId{std::move(id)}; }

    static DocumentId workspace(library::WorkspaceId id) { return DocumentId{std::move(id)}; }

    [[nodiscard]] const library::PlaylistId* playlist() const noexcept
    {
        return std::get_if<library::PlaylistId>(&value_);
    }

    [[nodiscard]] const library::WorkspaceId* workspace() const noexcept
    {
        return std::get_if<library::WorkspaceId>(&value_);
    }

    [[nodiscard]] bool is_queue() const noexcept
    {
        return std::holds_alternative<QueueDocumentId>(value_);
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return std::visit(
            [](const auto& id) {
                if constexpr (requires { id.valid(); })
                    return id.valid();
                else
                    return true;
            },
            value_
        );
    }

    auto operator<=>(const DocumentId&) const = default;

private:
    using Value = std::variant<QueueDocumentId, library::PlaylistId, library::WorkspaceId>;

    explicit DocumentId(Value value)
        : value_{std::move(value)}
    {
    }

    Value value_;
};
enum class TrackCommand : unsigned {
    play = 1,
    remove = 2,
    transfer = 4,
    move = 8,
    apply_order = 16,
    activate_destination = 32,
    append = 64,
};

class DocumentCapabilities final {
public:
    constexpr DocumentCapabilities() = default;

    constexpr DocumentCapabilities(std::initializer_list<TrackCommand> commands)
    {
        for (const auto command : commands)
            commands_ |= static_cast<unsigned>(command);
    }

    [[nodiscard]] constexpr bool supports(TrackCommand command) const noexcept
    {
        return (commands_ & static_cast<unsigned>(command)) != 0;
    }

    static constexpr DocumentCapabilities queue() noexcept
    {
        return {TrackCommand::play, TrackCommand::remove, TrackCommand::transfer,
            TrackCommand::apply_order};
    }

    static constexpr DocumentCapabilities playlist() noexcept
    {
        return {TrackCommand::play, TrackCommand::remove, TrackCommand::transfer,
            TrackCommand::move, TrackCommand::apply_order, TrackCommand::activate_destination};
    }

    static constexpr DocumentCapabilities workspace() noexcept
    {
        return {TrackCommand::play, TrackCommand::transfer, TrackCommand::apply_order,
            TrackCommand::append};
    }

    bool operator==(const DocumentCapabilities&) const = default;

private:
    unsigned commands_{0};
};

} // namespace wiola::gui
