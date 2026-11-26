/**
 * @file
 * @brief Application-owned playback preferences and one-time restoration.
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

#include "session_state.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <set>

namespace wiola::app {
namespace {

constexpr qsizetype max_bytes = 2 * 1024 * 1024;
using namespace gui;

bool text(const QJsonValue& value, std::string& output, qsizetype max = 4096)
{
    if (!value.isString())
        return false;
    const auto bytes = value.toString().toUtf8();
    if (bytes.size() > max || bytes.contains('\0'))
        return false;
    output = bytes.toStdString();
    return true;
}

bool number(const QJsonValue& value, int& output, int low, int high)
{
    if (!value.isDouble())
        return false;
    const auto number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < low || number > high)
        return false;
    output = static_cast<int>(number);
    return true;
}

bool keys(const QJsonObject& object, std::initializer_list<const char*> allowed)
{
    for (auto item = object.begin(); item != object.end(); ++item) {
        bool known = false;
        for (const auto key : allowed)
            if (item.key() == key) {
                known = true;
                break;
            }
        if (!known)
            return false;
    }
    return true;
}

QJsonObject document(const DocumentId& id)
{
    if (const auto* value = id.playlist())
        return {
            {"playlist", QString::fromStdString(value->value)}
        };
    if (const auto* value = id.workspace())
        return {
            {"workspace", QString::fromStdString(value->value)}
        };
    return {
        {"queue", true}
    };
}

std::optional<DocumentId> document(const QJsonValue& value)
{
    if (!value.isObject())
        return {};
    const auto object = value.toObject();
    if (object.size() != 1)
        return {};
    if (object.value("queue").isBool() && object.value("queue").toBool())
        return DocumentId::queue();
    std::string id;
    if (text(object.value("playlist"), id, 128) && !id.empty())
        return DocumentId::playlist(library::PlaylistId{id});
    if (text(object.value("workspace"), id, 128) && !id.empty())
        return DocumentId::workspace(library::WorkspaceId{id});
    return {};
}

QJsonObject occurrence(const TrackRowId& id)
{
    if (const auto* queue = std::get_if<engine::QueueEntryId>(&id))
        return {
            {"queue", QString::number(queue->value)}
        };
    if (const auto* entry = std::get_if<library::PlaylistEntryId>(&id))
        return {
            {"entry", QString::fromStdString(entry->value)}
        };
    const auto& item = std::get<WorkspaceTrackId>(id);
    return {
        {"workspace", QString::fromStdString(item.workspace.value)},
        {"media",     QString::fromStdString(item.media.value)    }
    };
}

std::optional<TrackRowId> occurrence(const QJsonValue& value)
{
    if (!value.isObject())
        return {};
    const auto object = value.toObject();
    std::string id;
    if (object.size() == 1 && text(object.value("queue"), id, 20)) {
        bool valid = false;
        const auto token = QString::fromStdString(id).toULongLong(&valid);
        if (valid && token && QString::number(token).toStdString() == id)
            return engine::QueueEntryId{token};
    }
    if (object.size() == 1 && text(object.value("entry"), id, 128) && !id.empty())
        return library::PlaylistEntryId{id};
    std::string media;
    if (object.size() == 2 && text(object.value("workspace"), id, 128) && !id.empty() &&
        text(object.value("media"), media) && !media.empty())
        return WorkspaceTrackId{library::WorkspaceId{id}, library::MediaId{media}};
    return {};
}

constexpr std::pair<StandardTrackColumn, const char*> standards[] = {
    {cover,  "cover" },
    {title,  "title" },
    {artist, "artist"},
    {album,  "album" },
    {length, "length"}
};

QJsonObject column(const TrackColumnId& id)
{
    if (const auto* standard = std::get_if<StandardTrackColumn>(&id)) {
        for (const auto& [candidate, key] : standards)
            if (candidate == *standard)
                return {
                    {"standard", key}
                };
        return {};
    }
    return {
        {"tag", QString::fromStdString(std::get<CustomTagColumn>(id).key)}
    };
}

std::optional<TrackColumnId> column(const QJsonValue& value)
{
    if (!value.isObject())
        return {};
    const auto object = value.toObject();
    if (object.size() != 1)
        return {};
    for (const auto& [id, key] : standards)
        if (object.value("standard").toString() == key)
            return id;
    std::string tag;
    if (text(object.value("tag"), tag, 128) && !tag.empty())
        return CustomTagColumn{tag};
    return {};
}

constexpr std::pair<TrackSortField, const char*> sort_fields[] = {
    {TrackSortField::title,      "title"   },
    {TrackSortField::artist,     "artist"  },
    {TrackSortField::album,      "album"   },
    {TrackSortField::duration,   "duration"},
    {TrackSortField::path,       "path"    },
    {TrackSortField::custom_tag, "tag"     }
};

QJsonObject browser(const gui::BrowserViewState& state)
{
    QJsonArray filters, sorts, columns, selected;
    for (const auto& filter : state.view.query.filters)
        filters.append(QJsonObject{
            {"key",   QString::fromStdString(filter.key)                                    },
            {"value", QString::fromStdString(filter.value)                                  },
            {"mode",  filter.mode == library::TagFilterMode::include ? "include" : "exclude"}
        });
    for (const auto& sort : state.view.sort_keys) {
        QString key;
        for (const auto& [id, name] : sort_fields)
            if (sort.field == id)
                key = name;
        sorts.append(QJsonObject{
            {"field",     key                                                            },
            {"direction",
             sort.direction == TrackSortDirection::ascending ? "ascending" : "descending"},
            {"tag",       QString::fromStdString(sort.tag_key)                           }
        });
    }
    for (const auto& [id, state] : state.columns)
        columns.append(QJsonObject{
            {"id",      column(id)   },
            {"width",   state.width  },
            {"order",   state.order  },
            {"visible", state.visible}
        });
    for (const auto& id : state.selected)
        selected.append(occurrence(id));
    const auto& query = state.view.query;
    return {
        {"query",
         QJsonObject{{"title", QString::fromStdString(query.title)},
                {"artist", QString::fromStdString(query.artist)},
                {"album", QString::fromStdString(query.album)},
                {"tags", QString::fromStdString(query.tags)}, {"filters", filters}}       },
        {"sort",     sorts                                                                },
        {"columns",  columns                                                              },
        {"selected", selected                                                             },
        {"current",  state.current ? QJsonValue{occurrence(*state.current)} : QJsonValue{}},
        {"anchor",   state.anchor ? QJsonValue{occurrence(*state.anchor)} : QJsonValue{}  },
        {"offset",   state.anchor_offset                                                  }
    };
}

bool browser(const QJsonValue& value, gui::BrowserViewState& output)
{
    if (!value.isObject())
        return false;
    const auto object = value.toObject();
    if (!keys(object, {"query", "sort", "columns", "selected", "current", "anchor", "offset"}) ||
        !object.value("query").isObject())
        return false;
    const auto query = object.value("query").toObject();
    auto& fields = output.view.query;
    if (!keys(query, {"title", "artist", "album", "tags", "filters"}) ||
        !text(query.value("title"), fields.title) || !text(query.value("artist"), fields.artist) ||
        !text(query.value("album"), fields.album) || !text(query.value("tags"), fields.tags))
        return false;
    const auto list = [&](const QJsonValue& value, int maximum) -> std::optional<QJsonArray> {
        if (!value.isArray() || value.toArray().size() > maximum)
            return {};
        return value.toArray();
    };
    const auto filters = list(query.value("filters"), 128), sorts = list(object.value("sort"), 8),
               columns = list(object.value("columns"), 128),
               selected = list(object.value("selected"), 10000);
    if (!filters || !sorts || !columns || !selected)
        return false;
    for (const auto& value : *filters) {
        if (!value.isObject())
            return false;
        const auto object = value.toObject();
        library::TagFilter filter;
        const auto mode = object.value("mode").toString();
        if (!keys(object, {"key", "value", "mode"}) ||
            !text(object.value("key"), filter.key, 128) || filter.key.empty() ||
            !text(object.value("value"), filter.value) || (mode != "include" && mode != "exclude"))
            return false;
        filter.mode =
            mode == "include" ? library::TagFilterMode::include : library::TagFilterMode::exclude;
        fields.filters.push_back(std::move(filter));
    }
    for (const auto& value : *sorts) {
        if (!value.isObject())
            return false;
        const auto object = value.toObject();
        if (!keys(object, {"field", "direction", "tag"}))
            return false;
        TrackSortKey key;
        bool known = false;
        for (const auto& [id, name] : sort_fields)
            if (object.value("field").toString() == name) {
                key.field = id;
                known = true;
                break;
            }
        const auto direction = object.value("direction").toString();
        if (!known || (direction != "ascending" && direction != "descending") ||
            !text(object.value("tag"), key.tag_key, 128) ||
            (key.field == TrackSortField::custom_tag ? key.tag_key.empty() : !key.tag_key.empty()))
            return false;
        key.direction = direction == "ascending" ? TrackSortDirection::ascending
                                                 : TrackSortDirection::descending;
        output.view.sort_keys.push_back(std::move(key));
    }
    std::set<int> orders;
    for (const auto& value : *columns) {
        if (!value.isObject())
            return false;
        const auto object = value.toObject();
        const auto id = column(object.value("id"));
        TrackColumnLayout state;
        if (!keys(object, {"id", "width", "order", "visible"}) || !id ||
            !number(object.value("width"), state.width, 0, 4096) ||
            !number(object.value("order"), state.order, 0, 127) ||
            !object.value("visible").isBool() || !orders.insert(state.order).second)
            return false;
        state.visible = object.value("visible").toBool();
        if (!output.columns.emplace(*id, state).second)
            return false;
    }
    std::set<TrackRowId> chosen;
    for (const auto& value : *selected) {
        const auto id = occurrence(value);
        if (!id || !chosen.insert(*id).second)
            return false;
        output.selected.push_back(*id);
    }
    for (const auto& [key, target] : {
             std::pair{"current", &output.current},
             std::pair{"anchor",  &output.anchor }
    }) {
        if (!object.value(key).isNull()) {
            *target = occurrence(object.value(key));
            if (!*target)
                return false;
        }
    }
    return number(object.value("offset"), output.anchor_offset, -1000000, 1000000);
}

QByteArray encode(const SessionState& state)
{
    QJsonArray open, browsers, splitters;
    for (const auto& id : state.documents.open)
        open.append(document(id));
    for (const auto& [id, value] : state.documents.browsers)
        browsers.append(QJsonObject{
            {"document", document(id)  },
            {"state",    browser(value)}
        });
    for (const auto& [id, sizes] : state.splitters) {
        QJsonArray values;
        for (const auto size : sizes)
            values.append(size);
        splitters.append(QJsonObject{
            {"id",    QString::fromStdString(id)},
            {"sizes", values                    }
        });
    }
    auto object = QJsonObject{
        {"version",     1                         },
        {"open",        open                      },
        {"visible",
         state.documents.visible ? QJsonValue{document(*state.documents.visible)}
                                    : QJsonValue{}},
        {"destination",
         state.documents.destination
                ? QJsonValue{QString::fromStdString(state.documents.destination->value)}
                : QJsonValue{}                    },
        {"browsers",    browsers                  },
        {"window",
         QJsonArray{state.window.x(), state.window.y(), state.window.width(),
                state.window.height()}            },
        {"splitters",   splitters                 },
        {"layout",      state.layout_identity     }
    };
    if (state.documents.last_playlist)
        object.insert("lastPlaylist", QString::fromStdString(state.documents.last_playlist->value));
    return QJsonDocument{object}.toJson(QJsonDocument::Compact);
}

} // namespace

SessionStateError read_session_state(QByteArrayView bytes, SessionState& output)
{
    if (bytes.size() > max_bytes)
        return SessionStateError::too_large;
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(QByteArray{bytes.data(), bytes.size()}, &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject())
        return SessionStateError::invalid;
    const auto object = parsed.object();
    if (!object.value("version").isDouble())
        return SessionStateError::invalid;
    const auto version = object.value("version").toDouble();
    if (!std::isfinite(version) || std::floor(version) != version)
        return SessionStateError::invalid;
    if (version != 1)
        return SessionStateError::unsupported_version;
    if (!keys(object,
            {"version", "open", "visible", "destination", "browsers", "window", "splitters",
                "layout", "lastPlaylist"}) ||
        !object.value("open").isArray() || object.value("open").toArray().size() > 128 ||
        !object.value("browsers").isArray() || object.value("browsers").toArray().size() > 128 ||
        !object.value("splitters").isArray() || object.value("splitters").toArray().size() > 128)
        return SessionStateError::invalid;
    SessionState next;
    next.documents.open.clear();
    std::set<gui::DocumentId> ids;
    for (const auto& value : object.value("open").toArray()) {
        const auto id = document(value);
        if (!id || !ids.insert(*id).second)
            return SessionStateError::invalid;
        next.documents.open.push_back(*id);
    }
    next.documents.visible.reset();
    if (!object.value("visible").isNull()) {
        next.documents.visible = document(object.value("visible"));
        if (!next.documents.visible)
            return SessionStateError::invalid;
    }
    if (!object.value("destination").isNull()) {
        std::string id;
        if (!text(object.value("destination"), id, 128) || id.empty())
            return SessionStateError::invalid;
        next.documents.destination = library::PlaylistId{id};
    }
    if (!object.value("lastPlaylist").isUndefined() && !object.value("lastPlaylist").isNull()) {
        std::string id;
        if (!text(object.value("lastPlaylist"), id, 128) || id.empty())
            return SessionStateError::invalid;
        next.documents.last_playlist = library::PlaylistId{id};
    }
    for (const auto& value : object.value("browsers").toArray()) {
        if (!value.isObject())
            return SessionStateError::invalid;
        const auto item = value.toObject();
        const auto id = document(item.value("document"));
        gui::BrowserViewState state;
        if (!keys(item, {"document", "state"}) || !id || !browser(item.value("state"), state) ||
            !next.documents.browsers.emplace(*id, std::move(state)).second)
            return SessionStateError::invalid;
    }
    const auto geometry = object.value("window");
    if (!geometry.isArray() || geometry.toArray().size() != 4)
        return SessionStateError::invalid;
    int values[4];
    for (int i = 0; i < 4; ++i)
        if (!number(geometry.toArray()[i], values[i], i < 2 ? -1000000 : 0,
                i < 2 ? 1000000 : 32768))
            return SessionStateError::invalid;
    next.window = {values[0], values[1], values[2], values[3]};
    for (const auto& value : object.value("splitters").toArray()) {
        if (!value.isObject())
            return SessionStateError::invalid;
        const auto item = value.toObject();
        std::string id;
        if (!keys(item, {"id", "sizes"}) || !text(item.value("id"), id, 128) || id.empty() ||
            !item.value("sizes").isArray() || item.value("sizes").toArray().size() > 32)
            return SessionStateError::invalid;
        std::vector<int> sizes;
        for (const auto& size : item.value("sizes").toArray()) {
            int number_value;
            if (!number(size, number_value, 0, 1000000))
                return SessionStateError::invalid;
            sizes.push_back(number_value);
        }
        if (!next.splitters.emplace(id, std::move(sizes)).second)
            return SessionStateError::invalid;
    }
    std::string layout;
    if (!text(object.value("layout"), layout, 64))
        return SessionStateError::invalid;
    next.layout_identity = QString::fromStdString(layout);
    output = std::move(next);
    return SessionStateError::none;
}

QByteArray write_session_state(const SessionState& state)
{
    if (state.documents.open.size() > 128 || state.documents.browsers.size() > 128 ||
        state.splitters.size() > 128)
        return {};
    for (const auto& [id, browser] : state.documents.browsers)
        if (browser.selected.size() > 10000 || browser.columns.size() > 128 ||
            browser.view.query.filters.size() > 128 || browser.view.sort_keys.size() > 8)
            return {};
    const auto bytes = encode(state);
    SessionState checked;
    if (read_session_state(bytes, checked) != SessionStateError::none || checked != state)
        return {};
    return bytes;
}

} // namespace wiola::app
