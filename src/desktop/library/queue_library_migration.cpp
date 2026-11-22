/**
 * @file
 * @brief Non-destructive migration of the QSettings queue into the library.
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

#include "queue_library_migration.hpp"

#include <library/location.hpp>
#include <persistence/qt/library_storage.hpp>
#include <platform/qt/player_settings.hpp>
#include <platform/qt/qt_path_adapter.hpp>

#include <QFile>
#include <QFileInfo>

#include <map>
#include <string>

namespace wiola::app {

QueueLibraryMigration::QueueLibraryMigration(QSettings& settings)
{
    const QString directory{QFileInfo{settings.fileName()}.absolutePath()};
    file_ = platform::qt::QtPathAdapter::from_qt(directory + "/library.json");
}

persistence::qt::LibraryJsonError QueueLibraryMigration::migrate(QSettings& settings) const
{
    persistence::qt::LibraryStorage storage{file_};
    const QString name{platform::qt::QtPathAdapter::to_qt(file_)};

    if (QFile::exists(name) || QFile::exists(name + ".bak")) {
        library::LibrarySnapshot existing;

        return storage.load(existing);
    }

    const platform::qt::PlaylistSettings queue{
        platform::qt::QtSettingsAdapter{settings}.read_playlist()};
    library::LibrarySnapshot library;
    library::PlaylistRecord migrated;
    migrated.id = {.value = "playlist-migrated-queue"};
    migrated.name = "Migrated queue";

    std::map<library::MediaId, std::size_t> known;

    for (const std::filesystem::path& path : queue.tracks) {
        const library::MediaId media{library::media_id_for(path)};

        if (!media.valid())
            continue;

        if (!known.contains(media)) {
            library::TrackRecord track;
            track.id = media;
            track.locations = {std::filesystem::path{media.value}};
            track.missing = !std::filesystem::exists(path);

            known.emplace(media, library.tracks.size());
            library.tracks.push_back(std::move(track));
        }

        migrated.entries.push_back(library::PlaylistEntryRecord{
            .id = {.value = "entry-" + std::to_string(migrated.entries.size() + 1)},
            .media = media});
    }

    if (!migrated.entries.empty())
        library.playlists.push_back(std::move(migrated));

    return storage.save(library);
}

} // namespace wiola::app
