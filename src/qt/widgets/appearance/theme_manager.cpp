/**
 * @file
 * @brief Semantic native light and dark application themes.
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

#include "theme_manager.hpp"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QScopeGuard>
#include <QThread>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <wiola/qt/paths.hpp>

namespace wiola::gui {
namespace {

QString choice_name(ThemeChoice choice)
{
    if (choice == ThemeChoice::custom)
        return "custom";
    for (const auto& preset : theme_presets())
        if (preset.choice == choice)
            return QString::fromLatin1(preset.key);
    return {};
}

std::optional<ThemeChoice> choice(const QString& key)
{
    if (key == "custom")
        return ThemeChoice::custom;
    for (const auto& preset : theme_presets())
        if (QString::fromLatin1(preset.key) == key)
            return preset.choice;
    return {};
}

ThemeState legacy_state(const QSettings& settings)
{
    ThemeState state;
    state.selected =
        choice(settings.value("theme/name", "light").toString()).value_or(ThemeChoice::light);
    for (const auto& descriptor : theme_color_descriptors()) {
        const auto saved = settings.value("theme/custom/" + QString::fromLatin1(descriptor.key));
        if (saved.isValid())
            theme_color(state.custom, descriptor.role) = QColor{saved.toString()};
    }
    if (!theme_contrast_error(state.custom).isEmpty())
        state.custom = theme_colors(Theme::light);
    return state;
}

} // namespace

QByteArray write_theme_json(const ThemeState& state)
{
    if (choice_name(state.selected).isEmpty() || !theme_contrast_error(state.custom).isEmpty() ||
        !valid_theme_style(state.custom_style))
        return {};
    QJsonObject colors;
    for (const auto& descriptor : theme_color_descriptors())
        colors.insert(descriptor.key,
            theme_color(state.custom, descriptor.role).name(QColor::HexRgb));
    QJsonObject style;
    for (const auto& metric : theme_style_metrics())
        style.insert(metric.key, state.custom_style.*metric.value);
    return QJsonDocument{
        QJsonObject{{"version", 2}, {"selected", choice_name(state.selected)}, {"custom", colors},
                    {"style", style}}
    }
        .toJson(QJsonDocument::Compact);
}

ThemeError read_theme_json(QByteArrayView bytes, ThemeState& output)
{
    if (bytes.size() > 16 * 1024)
        return ThemeError::invalid;
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(QByteArray{bytes.data(), bytes.size()}, &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject())
        return ThemeError::invalid;
    const auto object = parsed.object();
    if (!object.value("version").isDouble() || !std::isfinite(object.value("version").toDouble()) ||
        std::floor(object.value("version").toDouble()) != object.value("version").toDouble())
        return ThemeError::invalid;
    const double version = object.value("version").toDouble();
    if (version != 1 && version != 2)
        return ThemeError::unsupported_version;
    const auto selected = choice(object.value("selected").toString());
    if (object.size() != (version == 1 ? 3 : 4) || !selected || !object.value("custom").isObject())
        return ThemeError::invalid;
    const auto colors = object.value("custom").toObject();
    if (colors.size() != static_cast<qsizetype>(theme_color_descriptors().size()))
        return ThemeError::invalid;
    ThemeState candidate;
    candidate.selected = *selected;
    if (version == 2) {
        if (!object.value("style").isObject())
            return ThemeError::invalid;
        const auto style = object.value("style").toObject();
        if (style.size() != static_cast<qsizetype>(theme_style_metrics().size()))
            return ThemeError::invalid;
        for (const auto& metric : theme_style_metrics()) {
            const auto value = style.value(metric.key);
            if (!value.isDouble() || !std::isfinite(value.toDouble()) ||
                std::floor(value.toDouble()) != value.toDouble() ||
                value.toDouble() < metric.minimum || value.toDouble() > metric.maximum)
                return ThemeError::invalid;
            candidate.custom_style.*metric.value = value.toInt();
        }
    }
    for (const auto& descriptor : theme_color_descriptors()) {
        const auto value = colors.value(descriptor.key);
        if (!value.isString())
            return ThemeError::invalid;
        const auto key = value.toString();
        const QColor color{key};
        if (key.size() != 7 || !key.startsWith('#') || !color.isValid())
            return ThemeError::invalid;
        theme_color(candidate.custom, descriptor.role) = color;
    }
    if (!theme_contrast_error(candidate.custom).isEmpty())
        return ThemeError::invalid;
    output = std::move(candidate);
    return ThemeError::none;
}

ThemeManager::ThemeManager(const QSettings& legacy,
    persistence::qt::AtomicFileStore::OutputFactory output)
    : accepted_{legacy_state(legacy)}
    , legacy_{legacy.fileName(), QSettings::IniFormat}
    , store_{platform::qt::QtPathAdapter::from_qt(
                 QFileInfo{legacy.fileName()}.dir().filePath("theme.json")
             ),
          16 * 1024, std::move(output)}
{
    Q_ASSERT(QApplication::instance() &&
        QThread::currentThread() == QApplication::instance()->thread());
    last_good_ = write_theme_json(accepted_);
    for (const auto slot :
        {persistence::qt::FileSlot::primary, persistence::qt::FileSlot::backup}) {
        const auto read = store_.read(slot);
        if (!read.exists)
            continue;
        ThemeState candidate;
        const auto error = read.error == persistence::qt::FileStoreError::none
            ? read_theme_json(read.bytes, candidate)
            : ThemeError::io;
        if (error == ThemeError::none) {
            accepted_ = std::move(candidate);
            last_good_ = read.bytes;
            break;
        }
        if (error == ThemeError::unsupported_version)
            read_only_ = true;
        if (restore_error_ == ThemeError::none || error == ThemeError::unsupported_version)
            restore_error_ = error;
    }
    apply_theme(colors(), style());
}

std::shared_ptr<ThemeManager> ThemeManager::acquire(const QSettings& settings)
{
    Q_ASSERT(QApplication::instance() &&
        QThread::currentThread() == QApplication::instance()->thread());
    static std::weak_ptr<ThemeManager> active;
    auto manager = active.lock();
    if (manager && manager->legacy_.fileName() != settings.fileName())
        throw std::logic_error{"one appearance profile can be active at a time"};
    if (!manager) {
        manager = std::make_shared<ThemeManager>(settings);
        active = manager;
    }
    return manager;
}

void ThemeManager::check_owner() const
{
    Q_ASSERT(QThread::currentThread() == thread());
}

ThemeColors ThemeManager::colors() const
{
    check_owner();
    if (accepted_.selected == ThemeChoice::custom)
        return accepted_.custom;
    for (const auto& preset : theme_presets())
        if (preset.choice == accepted_.selected)
            return theme_colors(preset.palette);
    return theme_colors(Theme::light);
}

ThemeStyle ThemeManager::style() const
{
    check_owner();
    if (accepted_.selected == ThemeChoice::custom)
        return accepted_.custom_style;
    for (const auto& preset : theme_presets())
        if (preset.choice == accepted_.selected)
            return preset.style;
    return {};
}

ThemeError ThemeManager::select(ThemeChoice choice)
{
    check_owner();
    auto candidate = accepted_;
    candidate.selected = choice;
    return accept(std::move(candidate));
}

ThemeError ThemeManager::set_custom(const ThemeColors& colors)
{
    return set_custom(colors, style());
}

ThemeError ThemeManager::set_custom(const ThemeColors& colors, const ThemeStyle& style)
{
    check_owner();
    auto candidate = accepted_;
    candidate.custom = colors;
    candidate.custom_style = style;
    candidate.selected = ThemeChoice::custom;
    return accept(std::move(candidate));
}

ThemeError ThemeManager::import_json(QByteArrayView bytes)
{
    check_owner();
    ThemeState candidate;
    const auto error = read_theme_json(bytes, candidate);
    return error == ThemeError::none ? accept(std::move(candidate)) : error;
}

ThemeError ThemeManager::accept(ThemeState candidate)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (applying_)
        return ThemeError::busy;
    if (read_only_)
        return ThemeError::unsupported_version;
    const auto encoded = write_theme_json(candidate);
    ThemeState canonical;
    if (encoded.isEmpty() || read_theme_json(encoded, canonical) != ThemeError::none)
        return ThemeError::invalid;
    candidate = std::move(canonical);
    if (candidate == accepted_)
        return ThemeError::none;
    const QPointer<ThemeManager> owner{this};
    const auto transaction = qScopeGuard([owner] {
        if (owner)
            owner->applying_ = false;
    });
    applying_ = true;
    using namespace persistence::qt;
    if (store_.write(FileSlot::backup, last_good_) != FileStoreError::none ||
        store_.write(FileSlot::primary, encoded) != FileStoreError::none)
        return ThemeError::io;
    accepted_ = std::move(candidate);
    last_good_ = encoded;
    const auto palette = colors();
    const auto metrics = style();
    const auto generation = ++generation_;
    legacy_.remove("theme");
    legacy_.sync();
    apply_theme(palette, metrics);
    if (owner)
        emit theme_changed(generation);
    return ThemeError::none;
}

} // namespace wiola::gui
