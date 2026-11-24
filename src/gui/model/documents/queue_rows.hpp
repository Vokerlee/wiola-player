/**
 * @file
 * @brief Explicit playback-queue conversion at the Now Playing presentation boundary.
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
#include <gui/model/tracks/track_row.hpp>
#include <library/library_changes.hpp>
#include <library/repository.hpp>
#include <map>
#include <span>
#include <unordered_map>
#include <wiola/playback/queue_entry.hpp>
#include <wiola/playback/queue_view.hpp>

namespace wiola::gui {

struct QueueRowChanges {
    bool structure{}, current{}, playback_changed{};
    std::vector<TrackRow> rows;

    [[nodiscard]] bool changed() const { return structure || current || !rows.empty(); }
};

/// Queue occurrence projection and indexes, independently of widgets and playback commands.
class QueueRows final {
public:
    using Resolver = std::function<library::MediaId(const std::filesystem::path&)>;

    void set_resolver(Resolver resolver)
    {
        resolve_media_ = std::move(resolver);
        invalidate();
    }

    void invalidate() { structure_.reset(); }

    [[nodiscard]] QueueRowChanges reconcile(const engine::QueueView& playlist,
        const library::LibraryRepository* repository);
    [[nodiscard]] QueueRowChanges apply_library(const engine::QueueView& playlist,
        const library::LibraryRepository& repository, const library::LibraryChangeBatch& batch);

    [[nodiscard]] const std::vector<TrackRow>& rows() const { return rows_; }

    [[nodiscard]] std::optional<engine::QueueEntryId> current() const { return current_; }

private:
    [[nodiscard]] TrackRow present(const engine::QueueEntry& track,
        const library::LibraryRepository* repository) const;
    void replace(std::size_t position, TrackRow row);
    [[nodiscard]] std::vector<TrackRow> update_library(const engine::QueueView& playlist,
        const library::LibraryRepository& repository, const library::LibraryChangeBatch& batch);
    const engine::QueueView* playlist_{};
    std::optional<std::size_t> structure_, metadata_;
    std::optional<engine::QueueEntryId> current_;
    std::vector<TrackRow> rows_;
    std::vector<std::size_t> revisions_;
    std::map<engine::QueueEntryId, std::size_t> entries_;
    std::unordered_map<library::MediaId, std::vector<engine::QueueEntryId>> media_;
    std::unordered_map<std::string, std::vector<engine::QueueEntryId>> locations_;
    Resolver resolve_media_;
};

[[nodiscard]] TrackRow queue_row(const engine::QueueEntry& track);
[[nodiscard]] std::vector<TrackRow> queue_rows(std::span<const engine::QueueEntry> tracks);
[[nodiscard]] std::vector<engine::QueueEntryId> queue_entry_ids(std::span<const TrackRowId> rows);
[[nodiscard]] std::optional<engine::QueueEntryId>
queue_entry_id(const std::optional<TrackRowId>& row);

} // namespace wiola::gui
