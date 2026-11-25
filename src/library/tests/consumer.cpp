/**
 * @file
 * @brief Installed catalog contract and transaction consumer.
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

#include <wiola/library/in_memory_repository.hpp>
#include <wiola/library/metadata.hpp>
#include <wiola/library/metadata_patch.hpp>
#include <wiola/library/relocation.hpp>
#include <wiola/library/repository_transaction.hpp>
#include <wiola/library/search.hpp>
#include <wiola/library/snapshot.hpp>
#include <wiola/library/tags.hpp>

#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

#if __has_include(<media/local/provider.hpp>) || __has_include(<library/identity.hpp>)
#error "The installed catalog exposes source-tree or native-driver include roots"
#endif

namespace library = wiola::library;
static_assert(!std::is_convertible_v<library::MediaId, library::PlaylistEntryId>);
static_assert(std::is_const_v<std::remove_pointer_t<
        decltype(std::declval<const library::LibraryRepository&>().track(library::MediaId{}))>>);

int main()
{
    library::InMemoryRepository repository;
    library::TrackRecord track;
    track.id = {"opaque-media"};
    track.embedded.title = "Embedded title";
    track.locations = {"observed/track.flac"};
    if (!repository.put_track(track))
        return 1;
    bool persisted = false;
    std::size_t notifications = 0;
    auto subscription = repository.observe([&](const auto&) { ++notifications; });
    library::PlaylistId playlist;
    {
        library::RepositoryTransaction transaction{repository, [&](const auto& candidate) {
                                                       persisted =
                                                           candidate.track(track.id) != nullptr;
                                                       return persisted;
                                                   }};
        playlist = repository.create_playlist("Saved");
        const auto first = repository.append_track(playlist, track.id);
        const auto second = repository.append_track(playlist, track.id);
        if (!first || !second || *first == *second)
            return 2;
        track.overrides.title = "Owned title";
        if (!repository.put_track(track) ||
            !repository.add_tag(track.id, " Mood ", " Calm ").valid())
            return 3;
        const auto committed = transaction.commit();
        if (!committed || !committed.durable() || committed.revision != 1)
            return 4;
    }
    if (!persisted || notifications != 1 || repository.playlist(playlist)->entries.size() != 2)
        return 5;
    const auto* stored = repository.track(track.id);
    library::SearchQuery query;
    query.title = "OWNED";
    query.filters.push_back({.key = "mood", .value = "calm"});
    if (!stored || !library::matches_search(query, *stored) ||
        library::resolve_title(*stored) != "Owned title")
        return 6;
    std::unordered_map<library::MediaId, int> indexed{
        {track.id, 7}
    };
    if (indexed.at(track.id) != 7)
        return 7;
    const auto snapshot = library::snapshot_of(repository);
    {
        library::RepositoryTransaction rejected{repository, [](const auto&) { return false; }};
        repository.create_playlist("Rejected");
        if (rejected.commit())
            return 8;
    }
    if (repository.playlists() != snapshot.playlists || repository.change_revision() != 1 ||
        repository.staged_delta())
        return 9;
    auto receipt = std::make_shared<library::CommitReceipt>(2);
    auto accepted = library::CommitResult::accepted(2, receipt);
    bool durable = false;
    receipt->observe([&](library::DurabilityEvent event) {
        durable = event.durable && event.revision == 2;
    });
    receipt->notify(false);
    if (!accepted || accepted.durable() || durable)
        return 10;
    receipt->notify(true);
    if (!durable)
        return 11;
    return 0;
}
