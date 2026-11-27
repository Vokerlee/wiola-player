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

#pragma once
#ifndef Q_MOC_RUN
#include "theme.hpp"
#include <QSettings>
#include <memory>
#include <qt/storage/atomic_file_store.hpp>
#endif
#include <QObject>
#include <cstdint>

namespace wiola::gui {

struct ThemeState {
    ThemeChoice selected{ThemeChoice::light};
    ThemeColors custom{theme_colors(Theme::light)};
    ThemeStyle custom_style;
    bool operator==(const ThemeState&) const = default;
};
enum class ThemeError {
    none,
    invalid,
    unsupported_version,
    io,
    busy,
};
[[nodiscard]] QByteArray write_theme_json(const ThemeState& state);
[[nodiscard]] ThemeError read_theme_json(QByteArrayView bytes, ThemeState& output);

class ThemeManager final : public QObject {
    Q_OBJECT
public:
    explicit ThemeManager(const QSettings& legacy,
        persistence::qt::AtomicFileStore::OutputFactory output = {});
    [[nodiscard]] static std::shared_ptr<ThemeManager> acquire(const QSettings& settings);

    [[nodiscard]] ThemeState state() const
    {
        check_owner();
        return accepted_;
    }

    [[nodiscard]] ThemeColors colors() const;
    [[nodiscard]] ThemeStyle style() const;

    [[nodiscard]] std::uint64_t generation() const
    {
        check_owner();
        return generation_;
    }

    [[nodiscard]] ThemeError restore_error() const
    {
        check_owner();
        return restore_error_;
    }

    [[nodiscard]] ThemeError select(ThemeChoice choice);
    [[nodiscard]] ThemeError set_custom(const ThemeColors& colors);
    [[nodiscard]] ThemeError set_custom(const ThemeColors& colors, const ThemeStyle& style);
    [[nodiscard]] ThemeError import_json(QByteArrayView bytes);

    [[nodiscard]] QByteArray export_json() const
    {
        check_owner();
        return write_theme_json(accepted_);
    }

signals:
    void theme_changed(std::uint64_t generation);

private:
    void check_owner() const;
    ThemeError accept(ThemeState state);
    ThemeState accepted_;
    QSettings legacy_;
    persistence::qt::AtomicFileStore store_;
    QByteArray last_good_;
    std::uint64_t generation_{1};
    ThemeError restore_error_{ThemeError::none};
    bool read_only_{}, applying_{};
};

} // namespace wiola::gui
