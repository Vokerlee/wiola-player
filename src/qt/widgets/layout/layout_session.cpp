/**
 * @file
 * @brief Registered widget factories and recursive layout construction.
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

#include "layout_session.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>

namespace wiola::gui {

QString layout_choice_name(LayoutChoice choice)
{
    switch (choice) {
    case LayoutChoice::classic:
        return "classic";
    case LayoutChoice::library_focus:
        return "library-focus";
    case LayoutChoice::custom:
        return "custom";
    }
    return {};
}

std::optional<LayoutChoice> parse_layout_choice(const QString& key)
{
    // Retired built-in presets reopen with the current standard presentation.
    if (key == "queue-focus")
        return LayoutChoice::classic;
    for (const auto choice :
        {LayoutChoice::classic, LayoutChoice::library_focus, LayoutChoice::custom})
        if (layout_choice_name(choice) == key)
            return choice;
    return {};
}

namespace {

QByteArray encode(const LayoutDocument& document, LayoutChoice choice)
{
    const auto bytes = write_layout_json(document);
    if (bytes.isEmpty() || layout_choice_name(choice).isEmpty())
        return {};
    return QJsonDocument{
        QJsonObject{{"schema", 1}, {"choice", layout_choice_name(choice)},
                    {"layout", QJsonDocument::fromJson(bytes).object()}}
    }
        .toJson(QJsonDocument::Compact);
}

LayoutSessionError decode(const QByteArray& bytes, LayoutDocument& document, LayoutChoice& choice)
{
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject())
        return LayoutSessionError::invalid;
    const auto object = parsed.object();
    if (!object.value("schema").isDouble())
        return LayoutSessionError::invalid;
    if (object.value("schema").toDouble() != 1)
        return LayoutSessionError::unsupported_version;
    const auto identity = parse_layout_choice(object.value("choice").toString());
    if (object.size() != 3 || !identity || !object.value("layout").isObject())
        return LayoutSessionError::invalid;
    const auto result = read_layout_json(
        QJsonDocument{object.value("layout").toObject()}.toJson(QJsonDocument::Compact), document
    );
    if (result != LayoutJsonError::none)
        return result == LayoutJsonError::unsupported_version
            ? LayoutSessionError::unsupported_version
            : LayoutSessionError::invalid;
    choice = *identity;
    return LayoutSessionError::none;
}

} // namespace

LayoutSession::LayoutSession(std::filesystem::path file, LayoutDocument fallback,
    LayoutChoice choice, persistence::qt::AtomicFileStore::OutputFactory output)
    : accepted_{std::move(fallback)}
    , choice_{choice}
    , store_{std::move(file), 300 * 1024, std::move(output)}
    , last_good_{encode(accepted_, choice_)}
{
    if (last_good_.isEmpty())
        throw std::invalid_argument{"invalid fallback layout"};
    for (const auto slot :
        {persistence::qt::FileSlot::primary, persistence::qt::FileSlot::backup}) {
        const auto read = store_.read(slot);
        if (!read.exists)
            continue;
        LayoutDocument candidate;
        auto identity = choice_;
        const auto error = read.error == persistence::qt::FileStoreError::none
            ? decode(read.bytes, candidate, identity)
            : LayoutSessionError::read_failed;
        if (error == LayoutSessionError::none) {
            accepted_ = std::move(candidate);
            choice_ = identity;
            last_good_ = read.bytes;
            recovered_ = slot == persistence::qt::FileSlot::backup;
            return;
        }
        if (error == LayoutSessionError::unsupported_version)
            read_only_ = true;
        if (restore_error_ == LayoutSessionError::none ||
            error == LayoutSessionError::unsupported_version)
            restore_error_ = error;
    }
}

void LayoutSession::update_preset(LayoutDocument document)
{
    if (choice_ == LayoutChoice::custom)
        return;
    if (encode(document, choice_).isEmpty())
        throw std::invalid_argument{"invalid layout preset"};
    accepted_ = std::move(document);
}

void LayoutSession::use_fallback(LayoutDocument document, LayoutChoice choice)
{
    if (encode(document, choice).isEmpty())
        throw std::invalid_argument{"invalid layout fallback"};
    accepted_ = std::move(document);
    choice_ = choice;
    if (restore_error_ == LayoutSessionError::none)
        restore_error_ = LayoutSessionError::unavailable_views;
}

LayoutAcceptance LayoutSession::accept(LayoutDocument candidate, LayoutChoice choice,
    std::unique_ptr<PreparedLayout> prepared, QWidget* parent)
{
    if (read_only_)
        return {.error = LayoutSessionError::unsupported_version};
    const auto encoded = encode(candidate, choice);
    if (encoded.isEmpty() || !prepared ||
        write_layout_json(prepared->document()) != write_layout_json(candidate))
        return {.error = LayoutSessionError::invalid};
    if (!prepared->ready(parent))
        return {.error = LayoutSessionError::unavailable_views};
    using namespace persistence::qt;
    if (store_.write(FileSlot::backup, last_good_) != FileStoreError::none ||
        store_.write(FileSlot::primary, encoded) != FileStoreError::none)
        return {.error = LayoutSessionError::write_failed};
    const auto rollback = [&] {
        return store_.write(FileSlot::primary, last_good_) == FileStoreError::none
            ? LayoutSessionError::unavailable_views
            : LayoutSessionError::rollback_failed;
    };
    if (!prepared->ready(parent))
        return {.error = rollback()};
    auto* root = prepared->attach(parent);
    if (!root)
        return {.error = rollback()};
    accepted_ = std::move(candidate);
    choice_ = choice;
    last_good_ = encoded;
    restore_error_ = LayoutSessionError::none;
    recovered_ = false;
    return {.root = root};
}

} // namespace wiola::gui
