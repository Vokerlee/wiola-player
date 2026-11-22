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
#include <limits>
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
    const auto found = parse_component(text);
    if (!found)
        return false;
    output = *found;
    return true;
}

bool player_action(const QString& text, PlayerAction& output)
{
    const auto found = parse_action(text);
    if (!found)
        return false;
    output = *found;
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

bool valid_container_properties(const LayoutNode& node)
{
    const auto* descriptor = component_descriptor(node.kind);
    if (!descriptor)
        return false;
    if (node.orientation &&
        (!descriptor->supports(ComponentProperty::orientation) ||
            (*node.orientation != Qt::Horizontal && *node.orientation != Qt::Vertical)))
        return false;
    if (!node.sizes.empty() &&
        (!descriptor->supports(ComponentProperty::weights) ||
            node.sizes.size() != node.children.size() ||
            !std::ranges::all_of(node.sizes,
                [](int size) { return size > 0 && size <= max_dimension; })))
        return false;
    if (node.columns &&
        (!descriptor->supports(ComponentProperty::columns) || *node.columns < 1 ||
            *node.columns > static_cast<int>(max_children)))
        return false;
    return node.children.empty() || is_layout_container(node.kind);
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

        const QJsonValue orientation = object.value("orientation");
        if (!orientation.isUndefined()) {
            if (!orientation.isString() ||
                (orientation.toString() != "horizontal" && orientation.toString() != "vertical"))
                return LayoutJsonError::invalid_component;
            output.orientation =
                orientation.toString() == "horizontal" ? Qt::Horizontal : Qt::Vertical;
        }
        if (!read_dimension(object, "columns", output.columns))
            return LayoutJsonError::invalid_component;
        const QJsonValue sizes = object.value("sizes");
        if (!sizes.isUndefined()) {
            if (!sizes.isArray() || sizes.toArray().size() > static_cast<qsizetype>(max_children))
                return LayoutJsonError::invalid_component;
            for (const QJsonValue& size : sizes.toArray()) {
                const double number = size.toDouble(std::numeric_limits<double>::quiet_NaN());
                if (!std::isfinite(number) || number < 1 || number > max_dimension ||
                    std::floor(number) != number)
                    return LayoutJsonError::invalid_component;
                output.sizes.push_back(static_cast<int>(number));
            }
            if (output.kind != LayoutComponentKind::splitter)
                return LayoutJsonError::invalid_component;
        }

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
            return valid_container_properties(output) ? LayoutJsonError::none
                                                      : LayoutJsonError::invalid_component;

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

        return valid_container_properties(output) ? LayoutJsonError::none
                                                  : LayoutJsonError::invalid_component;
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

LayoutJsonError validate_layout_document(const LayoutDocument& document)
{
    if (document.version != 1)
        return LayoutJsonError::unsupported_version;

    QSet<QString> identities;
    std::size_t count = 0;

    const auto validate_node = [&]<typename Self>(Self&& self, const LayoutNode& node,
                                   int depth) -> LayoutJsonError {
        if (depth > max_depth || count >= max_nodes)
            return LayoutJsonError::limit_exceeded;

        if (!valid_id(node.id) || identities.contains(node.id) || node.title.toUtf8().size() > 128)
            return LayoutJsonError::invalid_component;

        const auto* descriptor = component_descriptor(node.kind);
        if (!descriptor)
            return LayoutJsonError::invalid_component;
        if (node.action &&
            (!descriptor->supports(ComponentProperty::action) || !action_descriptor(*node.action)))
            return LayoutJsonError::invalid_component;

        for (const auto* dimension :
            {&node.width, &node.height, &node.min_width, &node.min_height}) {
            if (*dimension && (**dimension < 0 || **dimension > max_dimension))
                return LayoutJsonError::invalid_component;
        }

        if (node.children.size() > max_children)
            return LayoutJsonError::invalid_component;
        if (!valid_container_properties(node))
            return LayoutJsonError::invalid_component;

        identities.insert(node.id);
        ++count;

        for (const LayoutNode& child : node.children) {
            const LayoutJsonError error = self(self, child, depth + 1);
            if (error != LayoutJsonError::none)
                return error;
        }

        return LayoutJsonError::none;
    };

    const LayoutJsonError result = validate_node(validate_node, document.root, 0);
    if (result != LayoutJsonError::none)
        return result;

    return count == document.node_count ? LayoutJsonError::none
                                        : LayoutJsonError::invalid_component;
}

bool is_layout_container(LayoutComponentKind kind) noexcept
{
    const auto* descriptor = component_descriptor(kind);
    return descriptor && descriptor->container;
}

QString layout_component_name(LayoutComponentKind kind)
{
    const auto* descriptor = component_descriptor(kind);
    return descriptor ? QString::fromLatin1(descriptor->key) : QString{};
}

QByteArray write_layout_json(const LayoutDocument& document)
{
    if (validate_layout_document(document) != LayoutJsonError::none)
        return {};

    const auto serialize = [](auto&& self, const LayoutNode& node) -> QJsonObject {
        QJsonObject result{
            {"id",   node.id                         },
            {"type", layout_component_name(node.kind)}
        };
        if (!node.title.isEmpty())
            result.insert("title", node.title);
        if (node.action)
            result.insert("action", action_name(*node.action));
        QJsonObject size;
        for (const auto& [key, value] : {
                 std::pair{"width",     node.width     },
                 {"height",    node.height    },
                 {"minWidth",  node.min_width },
                 {"minHeight", node.min_height}
        }) {
            if (value)
                size.insert(key, *value);
        }
        if (!size.isEmpty())
            result.insert("size", size);
        if (node.orientation)
            result.insert("orientation",
                *node.orientation == Qt::Horizontal ? "horizontal" : "vertical");
        if (node.columns)
            result.insert("columns", *node.columns);
        if (!node.sizes.empty()) {
            QJsonArray sizes;
            for (int value : node.sizes)
                sizes.append(value);
            result.insert("sizes", sizes);
        }
        if (!node.children.empty()) {
            QJsonArray children;
            for (const LayoutNode& child : node.children)
                children.append(self(self, child));
            result.insert("children", children);
        }
        return result;
    };
    const QByteArray json =
        QJsonDocument{
            QJsonObject{{"version", document.version},
                        {"root", serialize(serialize, document.root)}}
    }
            .toJson(QJsonDocument::Compact);
    return static_cast<std::size_t>(json.size()) <= max_document_bytes ? json : QByteArray{};
}

} // namespace wiola::gui
