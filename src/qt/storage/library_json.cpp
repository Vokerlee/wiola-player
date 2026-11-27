/**
 * @file
 * @brief Bounded versioned JSON for persistent library records.
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

#include "library_json.hpp"

#include <wiola/library/tags.hpp>
#include <wiola/qt/paths.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <string>

namespace wiola::persistence::qt {

namespace {

constexpr std::size_t max_document_bytes{max_library_document_bytes};
constexpr std::size_t max_tracks{10000};
constexpr std::size_t max_playlists{1000};
constexpr std::size_t max_workspaces{1000};
constexpr std::size_t max_playlist_entries{100000};
constexpr std::size_t max_locations{64};
constexpr std::size_t max_tags{library::max_tags_per_track};
constexpr std::size_t max_id_bytes{4096};
constexpr std::size_t max_text_bytes{library::max_tag_value_bytes};
constexpr std::size_t max_key_bytes{library::max_tag_key_bytes};
constexpr std::size_t max_name_bytes{256};
constexpr std::size_t max_fingerprint_bytes{256};

QString qt(const std::string& text)
{
    return QString::fromStdString(text);
}

bool read_text(const QJsonObject& object, const char* key, std::string& output,
    std::size_t max_bytes = max_text_bytes, bool required = false)
{
    const QJsonValue value{object.value(key)};

    if (value.isUndefined())
        return !required;

    if (!value.isString())
        return false;

    const QString text{value.toString()};

    if (static_cast<std::size_t>(text.toUtf8().size()) > max_bytes)
        return false;

    output = text.toStdString();

    return true;
}

bool read_override(const QJsonObject& object, const char* key, std::optional<std::string>& output)
{
    const QJsonValue value{object.value(key)};

    if (value.isUndefined() || value.isNull())
        return true;

    std::string text;

    if (!read_text(object, key, text))
        return false;

    output = std::move(text);

    return true;
}

bool read_number(const QJsonValue& value, std::optional<std::uint32_t>& output)
{
    if (value.isUndefined() || value.isNull())
        return true;

    if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0.0 ||
        std::floor(value.toDouble()) != value.toDouble() ||
        value.toDouble() > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
        return false;

    output = static_cast<std::uint32_t>(value.toDouble());

    return true;
}

bool read_number_override(const QJsonObject& object, const char* key,
    std::optional<std::optional<std::uint32_t>>& output)
{
    const QJsonValue value{object.value(key)};

    if (value.isUndefined())
        return true;

    output.emplace();

    return read_number(value, *output);
}

bool read_revision(const QJsonObject& object, const char* key, std::uint64_t& revision)
{
    std::string text;

    if (!read_text(object, key, text, 20))
        return false;

    if (text.empty())
        return true;

    const auto parsed{std::from_chars(text.data(), text.data() + text.size(), revision)};

    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

LibraryJsonError read_track(const QJsonValue& value, library::TrackRecord& output)
{
    if (!value.isObject())
        return LibraryJsonError::invalid_record;

    const QJsonObject object{value.toObject()};

    if (!read_text(object, "id", output.id.value, max_id_bytes, true) || !output.id.valid())
        return LibraryJsonError::invalid_record;

    if (object.contains("embedded")) {
        if (!object.value("embedded").isObject())
            return LibraryJsonError::invalid_record;

        const QJsonObject embedded{object.value("embedded").toObject()};

        if (!read_text(embedded, "title", output.embedded.title) ||
            !read_text(embedded, "artist", output.embedded.artist) ||
            !read_text(embedded, "album", output.embedded.album) ||
            !read_text(embedded, "genre", output.embedded.genre) ||
            !read_text(embedded, "comment", output.embedded.comment) ||
            !read_text(embedded, "composer", output.embedded.composer) ||
            !read_number(embedded.value("year"), output.embedded.year) ||
            !read_number(embedded.value("trackNumber"), output.embedded.track_number) ||
            !read_number(embedded.value("discNumber"), output.embedded.disc_number))
            return LibraryJsonError::invalid_record;

        const QJsonValue duration{embedded.value("durationSeconds")};

        if (!duration.isUndefined() && !duration.isNull()) {
            if (!duration.isDouble() || !std::isfinite(duration.toDouble()) ||
                duration.toDouble() < 0.0)
                return LibraryJsonError::invalid_record;

            output.embedded.duration = units::Time{duration.toDouble()};
        }
    }

    if (object.contains("overrides")) {
        if (!object.value("overrides").isObject())
            return LibraryJsonError::invalid_record;

        const QJsonObject overrides{object.value("overrides").toObject()};

        if (!read_override(overrides, "title", output.overrides.title) ||
            !read_override(overrides, "artist", output.overrides.artist) ||
            !read_override(overrides, "album", output.overrides.album) ||
            !read_override(overrides, "genre", output.overrides.genre) ||
            !read_override(overrides, "comment", output.overrides.comment) ||
            !read_override(overrides, "composer", output.overrides.composer) ||
            !read_number_override(overrides, "year", output.overrides.year) ||
            !read_number_override(overrides, "trackNumber", output.overrides.track_number) ||
            !read_number_override(overrides, "discNumber", output.overrides.disc_number))
            return LibraryJsonError::invalid_record;
    }

    if (object.contains("tags")) {
        if (!object.value("tags").isArray())
            return LibraryJsonError::invalid_record;

        const QJsonArray tags = object.value("tags").toArray();

        if (static_cast<std::size_t>(tags.size()) > max_tags)
            return LibraryJsonError::limit_exceeded;

        std::set<library::TagId> identities;
        std::set<std::pair<std::string, std::string>> values;

        for (const QJsonValue& tag_value : tags) {
            if (!tag_value.isObject())
                return LibraryJsonError::invalid_record;

            library::TagRecord tag;
            const QJsonObject tag_object{tag_value.toObject()};

            if (!read_text(tag_object, "id", tag.id.value, max_name_bytes, true) ||
                !read_text(tag_object, "key", tag.key, max_key_bytes, true) ||
                !read_text(tag_object, "value", tag.value, max_text_bytes, true) ||
                !tag.id.valid() || !identities.insert(tag.id).second)
                return LibraryJsonError::invalid_record;

            auto value = library::make_tag_value(std::move(tag.key), std::move(tag.value));

            if (!value ||
                !values
                    .emplace(library::normalized_tag_text(value->key),
                        library::normalized_tag_text(value->value))
                    .second)
                return LibraryJsonError::invalid_record;

            tag.key = std::move(value->key);
            tag.value = std::move(value->value);
            output.tags.push_back(std::move(tag));
        }
    }

    if (object.contains("locations")) {
        if (!object.value("locations").isArray())
            return LibraryJsonError::invalid_record;

        const QJsonArray locations = object.value("locations").toArray();

        if (static_cast<std::size_t>(locations.size()) > max_locations)
            return LibraryJsonError::limit_exceeded;

        for (const QJsonValue& location : locations) {
            if (!location.isString() ||
                static_cast<std::size_t>(location.toString().toUtf8().size()) > max_id_bytes)
                return LibraryJsonError::invalid_record;

            output.locations.push_back(platform::qt::QtPathAdapter::from_qt(location.toString()));
        }
    }

    if (!read_text(object, "fingerprint", output.fingerprint, max_fingerprint_bytes) ||
        !read_revision(object, "contentRevision", output.content_revision) ||
        !read_revision(object, "metadataRevision", output.metadata_revision))
        return LibraryJsonError::invalid_record;

    if (object.contains("missing")) {
        if (!object.value("missing").isBool())
            return LibraryJsonError::invalid_record;

        output.missing = object.value("missing").toBool();
    }

    return {};
}

LibraryJsonError read_playlist(const QJsonValue& value, const std::set<library::MediaId>& media,
    library::PlaylistRecord& output, std::size_t& total_entries)
{
    if (!value.isObject())
        return LibraryJsonError::invalid_record;

    const QJsonObject object{value.toObject()};

    if (!read_text(object, "id", output.id.value, max_name_bytes, true) ||
        !read_text(object, "name", output.name, max_name_bytes) || !output.id.valid() ||
        !object.value("entries").isArray())
        return LibraryJsonError::invalid_record;

    const QJsonArray entries = object.value("entries").toArray();
    total_entries += static_cast<std::size_t>(entries.size());

    if (total_entries > max_playlist_entries)
        return LibraryJsonError::limit_exceeded;

    std::set<library::PlaylistEntryId> identities;

    for (const QJsonValue& entry_value : entries) {
        if (!entry_value.isObject())
            return LibraryJsonError::invalid_record;

        library::PlaylistEntryRecord entry;
        const QJsonObject entry_object{entry_value.toObject()};

        if (!read_text(entry_object, "id", entry.id.value, max_name_bytes, true) ||
            !read_text(entry_object, "media", entry.media.value, max_id_bytes, true) ||
            !entry.id.valid() || !entry.media.valid() || !media.contains(entry.media) ||
            !identities.insert(entry.id).second)
            return LibraryJsonError::invalid_record;

        output.entries.push_back(std::move(entry));
    }

    return {};
}

LibraryJsonError read_workspace(const QJsonValue& value, library::WorkspaceRecord& output)
{
    if (!value.isObject())
        return LibraryJsonError::invalid_record;

    const QJsonObject object{value.toObject()};
    std::string root;

    if (!read_text(object, "id", output.id.value, max_name_bytes, true) ||
        !read_text(object, "root", root, max_id_bytes, true) || !output.id.valid() || root.empty())
        return LibraryJsonError::invalid_record;

    output.root = platform::qt::QtPathAdapter::from_qt(QString::fromStdString(root));
    output.recursive = true;

    if (object.contains("recursive")) {
        if (!object.value("recursive").isBool())
            return LibraryJsonError::invalid_record;

        output.recursive = object.value("recursive").toBool();
    }

    return LibraryJsonError::none;
}

QJsonObject write_track(const library::TrackRecord& track)
{
    QJsonObject embedded{
        {"title",    qt(track.embedded.title)   },
        {"artist",   qt(track.embedded.artist)  },
        {"album",    qt(track.embedded.album)   },
        {"genre",    qt(track.embedded.genre)   },
        {"comment",  qt(track.embedded.comment) },
        {"composer", qt(track.embedded.composer)}
    };

    if (track.embedded.duration)
        embedded.insert("durationSeconds", track.embedded.duration->seconds());
    else
        embedded.insert("durationSeconds", QJsonValue::Null);

    embedded.insert("year",
        track.embedded.year ? QJsonValue{static_cast<qint64>(*track.embedded.year)}
                            : QJsonValue::Null);
    embedded.insert("trackNumber",
        track.embedded.track_number ? QJsonValue{static_cast<qint64>(*track.embedded.track_number)}
                                    : QJsonValue::Null);
    embedded.insert("discNumber",
        track.embedded.disc_number ? QJsonValue{static_cast<qint64>(*track.embedded.disc_number)}
                                   : QJsonValue::Null);

    QJsonObject overrides;

    if (track.overrides.title)
        overrides.insert("title", qt(*track.overrides.title));

    if (track.overrides.artist)
        overrides.insert("artist", qt(*track.overrides.artist));

    if (track.overrides.album)
        overrides.insert("album", qt(*track.overrides.album));

    if (track.overrides.genre)
        overrides.insert("genre", qt(*track.overrides.genre));

    if (track.overrides.comment)
        overrides.insert("comment", qt(*track.overrides.comment));

    if (track.overrides.composer)
        overrides.insert("composer", qt(*track.overrides.composer));

    const auto write_number_override = [&overrides](const char* name, const auto& value) {
        if (!value)
            return;

        overrides.insert(name,
            *value ? QJsonValue{static_cast<qint64>(**value)} : QJsonValue::Null);
    };

    write_number_override("year", track.overrides.year);
    write_number_override("trackNumber", track.overrides.track_number);
    write_number_override("discNumber", track.overrides.disc_number);

    QJsonArray tags;

    for (const library::TagRecord& tag : track.tags)
        tags.push_back(QJsonObject{
            {"id",    qt(tag.id.value)},
            {"key",   qt(tag.key)     },
            {"value", qt(tag.value)   }
        });

    QJsonArray locations;

    for (const std::filesystem::path& location : track.locations)
        locations.push_back(platform::qt::QtPathAdapter::to_qt(location));

    return {
        {"id",               qt(track.id.value)                      },
        {"embedded",         embedded                                },
        {"overrides",        overrides                               },
        {"tags",             tags                                    },
        {"locations",        locations                               },
        {"fingerprint",      qt(track.fingerprint)                   },
        {"contentRevision",  QString::number(track.content_revision) },
        {"metadataRevision", QString::number(track.metadata_revision)},
        {"missing",          track.missing                           }
    };
}

QJsonObject write_playlist(const library::PlaylistRecord& playlist)
{
    QJsonArray entries;

    for (const library::PlaylistEntryRecord& entry : playlist.entries)
        entries.push_back(QJsonObject{
            {"id",    qt(entry.id.value)   },
            {"media", qt(entry.media.value)}
        });

    return {
        {"id",      qt(playlist.id.value)},
        {"name",    qt(playlist.name)    },
        {"entries", entries              }
    };
}

QJsonObject write_workspace(const library::WorkspaceRecord& workspace)
{
    return {
        {"id",        qt(workspace.id.value)                            },
        {"root",      platform::qt::QtPathAdapter::to_qt(workspace.root)},
        {"recursive", workspace.recursive                               }
    };
}

} // namespace

LibraryJsonError write_library_json(const library::LibrarySnapshot& library, QByteArray& output)
{
    QJsonArray tracks;

    for (const library::TrackRecord& track : library.tracks)
        tracks.push_back(write_track(track));

    QJsonArray playlists;

    for (const library::PlaylistRecord& playlist : library.playlists)
        playlists.push_back(write_playlist(playlist));

    QJsonArray workspaces;

    for (const library::WorkspaceRecord& workspace : library.workspaces)
        workspaces.push_back(write_workspace(workspace));

    const QJsonObject root{
        {"version",    1         },
        {"tracks",     tracks    },
        {"playlists",  playlists },
        {"workspaces", workspaces}
    };
    const QByteArray json{QJsonDocument{root}.toJson(QJsonDocument::Compact)};

    if (static_cast<std::size_t>(json.size()) > max_document_bytes)
        return LibraryJsonError::too_large;

    library::LibrarySnapshot validated;
    const LibraryJsonError error{read_library_json(json, validated)};

    if (error != LibraryJsonError::none)
        return error;

    output = json;

    return LibraryJsonError::none;
}

LibraryJsonError read_library_json(QByteArrayView json, library::LibrarySnapshot& output)
{
    if (static_cast<std::size_t>(json.size()) > max_document_bytes)
        return LibraryJsonError::too_large;

    QJsonParseError parse_error;
    const QJsonDocument document{
        QJsonDocument::fromJson(QByteArray{json.data(), json.size()}, &parse_error)};

    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return LibraryJsonError::malformed;

    const QJsonObject root{document.object()};
    const QJsonValue version{root.value("version")};

    if (!version.isDouble() || std::floor(version.toDouble()) != version.toDouble())
        return LibraryJsonError::malformed;

    if (version.toInt() != 1)
        return LibraryJsonError::unsupported_version;

    if (!root.value("tracks").isArray() || !root.value("playlists").isArray() ||
        (root.contains("workspaces") && !root.value("workspaces").isArray()))
        return LibraryJsonError::malformed;

    const QJsonArray tracks = root.value("tracks").toArray();
    const QJsonArray playlists = root.value("playlists").toArray();
    const QJsonArray workspaces = root.value("workspaces").toArray();

    if (static_cast<std::size_t>(tracks.size()) > max_tracks ||
        static_cast<std::size_t>(playlists.size()) > max_playlists ||
        static_cast<std::size_t>(workspaces.size()) > max_workspaces)
        return LibraryJsonError::limit_exceeded;

    library::LibrarySnapshot library;
    std::set<library::MediaId> media;

    for (const QJsonValue& value : tracks) {
        library::TrackRecord track;
        const LibraryJsonError result{read_track(value, track)};

        if (result != LibraryJsonError::none)
            return result;

        if (!media.insert(track.id).second)
            return LibraryJsonError::invalid_record;

        library.tracks.push_back(std::move(track));
    }

    std::set<library::PlaylistId> playlist_ids;
    std::size_t total_entries{0};

    for (const QJsonValue& value : playlists) {
        library::PlaylistRecord playlist;
        const LibraryJsonError result{read_playlist(value, media, playlist, total_entries)};

        if (result != LibraryJsonError::none)
            return result;

        if (!playlist_ids.insert(playlist.id).second)
            return LibraryJsonError::invalid_record;

        library.playlists.push_back(std::move(playlist));
    }

    std::set<library::WorkspaceId> workspace_ids;

    for (const QJsonValue& value : workspaces) {
        library::WorkspaceRecord workspace;
        const LibraryJsonError result{read_workspace(value, workspace)};

        if (result != LibraryJsonError::none)
            return result;

        if (!workspace_ids.insert(workspace.id).second)
            return LibraryJsonError::invalid_record;

        library.workspaces.push_back(std::move(workspace));
    }

    output = std::move(library);

    return LibraryJsonError::none;
}

} // namespace wiola::persistence::qt
