/**
 * @file
 * @brief Bounded versioned JSON description of a player layout.
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

#include "layout_document.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace wiola::gui {

namespace {

constexpr std::size_t max_document_bytes{256 * 1024};
constexpr std::size_t max_nodes{256};
constexpr std::size_t max_children{32};
constexpr int max_depth{16};
constexpr int max_dimension{4096};

bool valid_id(const QString& id)
{
    if (id.isEmpty() || id.toUtf8().size() > 64)
        return false;

    return std::ranges::all_of(id, [](const QChar letter) {
        const ushort value = letter.unicode();
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '-' || value == '_';
    });
}

bool component_kind(const QString& text, LayoutComponentKind& output)
{
    static constexpr std::array kinds{
        std::pair{"row",         LayoutComponentKind::row        },
        std::pair{"column",      LayoutComponentKind::column     },
        std::pair{"stack",       LayoutComponentKind::stack      },
        std::pair{"group",       LayoutComponentKind::group      },
        std::pair{"button",      LayoutComponentKind::button     },
        std::pair{"track_table", LayoutComponentKind::track_table},
        std::pair{"inspector",   LayoutComponentKind::inspector  },
        std::pair{"search",      LayoutComponentKind::search     },
        std::pair{"tags",        LayoutComponentKind::tags       },
        std::pair{"transport",   LayoutComponentKind::transport  },
        std::pair{"volume",      LayoutComponentKind::volume     },
        std::pair{"status",      LayoutComponentKind::status     },
    };

    const auto found =
        std::ranges::find_if(kinds, [&text](const auto& entry) { return text == entry.first; });
    if (found == kinds.end())
        return false;

    output = found->second;
    return true;
}

bool player_action(const QString& text, PlayerAction& output)
{
    static constexpr std::array actions{
        std::pair{"open",      PlayerAction::open     },
        std::pair{"previous",  PlayerAction::previous },
        std::pair{"play",      PlayerAction::play     },
        std::pair{"next",      PlayerAction::next     },
        std::pair{"stop",      PlayerAction::stop     },
        std::pair{"repeat",    PlayerAction::repeat   },
        std::pair{"shuffle",   PlayerAction::shuffle  },
        std::pair{"add",       PlayerAction::add      },
        std::pair{"remove",    PlayerAction::remove   },
        std::pair{"clear",     PlayerAction::clear    },
        std::pair{"equalizer", PlayerAction::equalizer},
    };

    const auto found =
        std::ranges::find_if(actions, [&text](const auto& entry) { return text == entry.first; });
    if (found == actions.end())
        return false;

    output = found->second;
    return true;
}

bool read_dimension(const QJsonObject& object, const char* key, std::optional<int>& output)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
        return true;

    const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(number) || number < 0 || number > max_dimension ||
        std::floor(number) != number)
        return false;

    output = static_cast<int>(number);
    return true;
}

struct Parser {
    LayoutDocument document;
    QSet<QString> identities;

    LayoutJsonError node(const QJsonValue& value, LayoutNode& output, int depth)
    {
        if (depth > max_depth || document.node_count >= max_nodes)
            return LayoutJsonError::limit_exceeded;

        if (!value.isObject())
            return LayoutJsonError::invalid_component;

        const QJsonObject object = value.toObject();
        const QString id = object.value("id").toString();
        if (!valid_id(id) || identities.contains(id))
            return LayoutJsonError::invalid_component;

        if (!component_kind(object.value("type").toString(), output.kind))
            return LayoutJsonError::invalid_component;

        const QJsonValue title = object.value("title");
        if (!title.isUndefined() && (!title.isString() || title.toString().toUtf8().size() > 128))
            return LayoutJsonError::invalid_component;
        output.title = title.toString();

        output.id = id;
        identities.insert(id);
        ++document.node_count;

        const QJsonValue action_value = object.value("action");
        if (!action_value.isUndefined()) {
            if (output.kind != LayoutComponentKind::button || !action_value.isString())
                return LayoutJsonError::invalid_component;

            PlayerAction action;
            if (!player_action(action_value.toString(), action))
                return LayoutJsonError::invalid_component;
            output.action = action;
        }

        const QJsonValue size_value = object.value("size");
        if (!size_value.isUndefined()) {
            if (!size_value.isObject())
                return LayoutJsonError::invalid_component;

            const QJsonObject size = size_value.toObject();
            if (!read_dimension(size, "width", output.width) ||
                !read_dimension(size, "height", output.height) ||
                !read_dimension(size, "minWidth", output.min_width) ||
                !read_dimension(size, "minHeight", output.min_height))
                return LayoutJsonError::invalid_component;
        }

        const QJsonValue children_value = object.value("children");
        if (children_value.isUndefined())
            return LayoutJsonError::none;

        if (!children_value.isArray() ||
            children_value.toArray().size() > static_cast<qsizetype>(max_children))
            return LayoutJsonError::invalid_component;

        const QJsonArray children = children_value.toArray();
        output.children.reserve(children.size());

        for (const QJsonValue& child : children) {
            LayoutNode nested;
            const LayoutJsonError error = node(child, nested, depth + 1);
            if (error != LayoutJsonError::none)
                return error;
            output.children.push_back(std::move(nested));
        }

        return LayoutJsonError::none;
    }
};

} // namespace

LayoutJsonError read_layout_json(QByteArrayView json, LayoutDocument& output)
{
    if (static_cast<std::size_t>(json.size()) > max_document_bytes)
        return LayoutJsonError::too_large;

    QJsonParseError error;
    const QJsonDocument parsed =
        QJsonDocument::fromJson(QByteArray{json.data(), json.size()}, &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject())
        return LayoutJsonError::malformed;

    const QJsonObject object = parsed.object();
    const double version =
        object.value("version").toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(version) || std::floor(version) != version)
        return LayoutJsonError::malformed;
    if (version != 1)
        return LayoutJsonError::unsupported_version;

    Parser parser;
    parser.document.version = 1;
    const LayoutJsonError result = parser.node(object.value("root"), parser.document.root, 0);

    if (result != LayoutJsonError::none)
        return result;

    output = std::move(parser.document);
    return LayoutJsonError::none;
}

} // namespace wiola::gui
