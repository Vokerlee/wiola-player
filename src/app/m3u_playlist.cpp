/**
 * @file
 * @brief Bounded UTF-8 M3U8 import and export for saved playlists.
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

#include "m3u_playlist.hpp"

#include <gui/qt_path_adapter.hpp>

#include <library/location.hpp>

#include <QLocale>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>

namespace wiola::app {

namespace {

constexpr std::size_t max_m3u_bytes{16 * 1024 * 1024};
constexpr std::size_t max_m3u_entries{100000};

QString safe_metadata(const std::string& value)
{
    return QString::fromStdString(value).replace('\n', ' ').replace('\r', ' ');
}

bool metadata_empty(const media::TrackMetadata& metadata)
{
    return metadata.title.empty() && metadata.artist.empty() && metadata.album.empty() &&
        !metadata.duration;
}

bool parse_extinf(const QString& line, media::TrackMetadata& metadata)
{
    const qsizetype comma = line.indexOf(',', 8);
    if (comma < 0)
        return false;

    bool valid = false;
    const double seconds = QLocale::c().toDouble(line.sliced(8, comma - 8), &valid);
    if (!valid || !std::isfinite(seconds) || seconds < -1.0)
        return false;

    metadata.duration =
        seconds >= 0.0 ? std::optional<units::Time>{units::Time{seconds}} : std::nullopt;
    const QString description = line.sliced(comma + 1);
    const qsizetype separator = description.indexOf(" - ");

    if (separator < 0)
        metadata.title = description.toStdString();
    else {
        metadata.artist = description.left(separator).toStdString();
        metadata.title = description.sliced(separator + 3).toStdString();
    }

    return true;
}

} // namespace

M3uError M3uPlaylist::export_playlist(const library::LibraryRepository& repository,
    const library::PlaylistId& id, const std::filesystem::path& directory, QByteArray& output)
{
    const library::PlaylistRecord* playlist = repository.playlist(id);
    if (playlist == nullptr)
        return M3uError::missing_playlist;

    if (playlist->entries.size() > max_m3u_entries)
        return M3uError::limit_exceeded;

    QByteArray encoded{"#EXTM3U\n"};

    for (const library::PlaylistEntryRecord& entry : playlist->entries) {
        const library::TrackRecord* media = repository.track(entry.media);
        if (media == nullptr || media->locations.empty())
            return M3uError::invalid_record;

        const std::filesystem::path& location = media->locations.front();
        std::filesystem::path relative = location.lexically_relative(directory);
        if (relative.empty())
            relative = location;

        const QString path = gui::QtPathAdapter::to_qt(relative);
        if (path.contains('\n') || path.contains('\r'))
            return M3uError::invalid_record;

        const double seconds =
            media->embedded.duration ? media->embedded.duration->seconds() : -1.0;
        const QString description = safe_metadata(media->display_artist()).isEmpty()
            ? safe_metadata(media->display_title())
            : safe_metadata(media->display_artist()) + " - " +
                safe_metadata(media->display_title());

        encoded += "#EXTINF:" + QLocale::c().toString(seconds, 'f', 3).toUtf8() + "," +
            description.toUtf8() + "\n" + path.toUtf8() + "\n";

        if (static_cast<std::size_t>(encoded.size()) > max_m3u_bytes)
            return M3uError::too_large;
    }

    output = std::move(encoded);
    return M3uError::none;
}

M3uError M3uPlaylist::import_playlist(library::InMemoryRepository& repository,
    const std::filesystem::path& directory, const std::string& name, QByteArrayView input,
    library::PlaylistId& output)
{
    if (static_cast<std::size_t>(input.size()) > max_m3u_bytes)
        return M3uError::too_large;

    if (QString::fromStdString(name).trimmed().isEmpty())
        return M3uError::invalid_record;

    QStringDecoder decoder{QStringDecoder::Utf8};
    const QString text = decoder(input);
    if (decoder.hasError())
        return M3uError::malformed;

    const QList<QString> lines = text.split('\n', Qt::KeepEmptyParts);
    if (static_cast<std::size_t>(lines.size()) > 2 * max_m3u_entries + 1)
        return M3uError::limit_exceeded;

    library::InMemoryRepository updated = repository;
    const library::PlaylistId playlist =
        updated.create_playlist(QString::fromStdString(name).trimmed().toStdString());
    media::TrackMetadata pending;
    bool has_pending = false;
    std::size_t entries = 0;

    for (QString line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);

        if (line.trimmed().isEmpty() || (line.startsWith('#') && !line.startsWith("#EXTINF:")))
            continue;

        if (line.startsWith("#EXTINF:")) {
            pending = {};
            has_pending = parse_extinf(line, pending);

            if (!has_pending)
                return M3uError::malformed;
            continue;
        }

        if (++entries > max_m3u_entries)
            return M3uError::limit_exceeded;

        std::filesystem::path location = gui::QtPathAdapter::from_qt(line);
        if (!location.is_absolute())
            location = directory / location;

        const library::MediaId media = library::media_id_for(location);
        if (!media.valid())
            return M3uError::invalid_record;

        library::TrackRecord record;
        if (const library::TrackRecord* existing = updated.track(media))
            record = *existing;

        record.id = media;
        if (std::ranges::find(record.locations, location) == record.locations.end())
            record.locations.push_back(location);

        std::error_code error;
        record.missing = !std::filesystem::exists(location, error);

        if (has_pending && metadata_empty(record.embedded))
            record.embedded = pending;

        if (!updated.put_track(std::move(record)) || !updated.append_track(playlist, media))
            return M3uError::invalid_record;

        pending = {};
        has_pending = false;
    }

    if (!repository.replace_all(updated.tracks(), updated.playlists(), updated.workspaces()))
        return M3uError::invalid_record;

    output = playlist;
    return M3uError::none;
}

} // namespace wiola::app
