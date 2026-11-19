/**
 * @file
 * @brief Merge learned metadata without replacing user-owned record layers.
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

#include "metadata_merge.hpp"

namespace wiola::app {

bool MetadataMerge::merge(library::InMemoryRepository& repository, const library::MediaId& media,
    const wiola::media::Metadata& learned) const
{
    const library::TrackRecord* existing = repository.track(media);
    if (existing == nullptr)
        return false;

    library::TrackRecord updated = *existing;
    bool changed = false;

    const auto learn = [&changed](std::string& stored, const std::string& incoming) {
        if (!incoming.empty() && stored != incoming) {
            stored = incoming;
            changed = true;
        }
    };

    learn(updated.embedded.title, learned.track.title);
    learn(updated.embedded.artist, learned.track.artist);
    learn(updated.embedded.album, learned.track.album);
    learn(updated.embedded.genre, learned.track.genre);
    learn(updated.embedded.comment, learned.track.comment);
    learn(updated.embedded.composer, learned.track.composer);

    const auto learn_number = [&changed](auto& stored, const auto& incoming) {
        if (incoming && stored != incoming) {
            stored = incoming;
            changed = true;
        }
    };

    learn_number(updated.embedded.duration, learned.track.duration);
    learn_number(updated.embedded.year, learned.track.year);
    learn_number(updated.embedded.track_number, learned.track.track_number);
    learn_number(updated.embedded.disc_number, learned.track.disc_number);

    if (!changed)
        return false;

    ++updated.metadata_revision;
    return repository.put_track(std::move(updated));
}

} // namespace wiola::app
