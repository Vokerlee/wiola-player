/**
 * @file
 * @brief A length of time, written the way a listener reads it.
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
#include <QPointer>
#include <QString>
#include <QWidget>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>
#include <wiola/library/identity.hpp>

namespace wiola::gui {

enum class PlayerFileChoice {
    import_playlist,
    export_playlist,
    import_layout,
    export_layout,
    import_theme,
    export_theme,
};

struct FileDialogSpecification {
    QString title;
    QString filter;
    bool save{};
};

/// Dialog wording and file types are independent of the operating-system picker.
[[nodiscard]] FileDialogSpecification file_dialog_specification(PlayerFileChoice purpose);

struct PlaylistChoice {
    library::PlaylistId id;
    std::string name;
};

/// GUI boundary returning owned choices; callers capture command targets before invoking it.
class PlayerDialogs {
public:
    virtual ~PlayerDialogs() = default;
    virtual std::vector<std::filesystem::path> tracks(std::span<const std::string> extensions) = 0;
    virtual std::optional<std::filesystem::path> folder() = 0;
    virtual std::optional<std::filesystem::path> file(PlayerFileChoice purpose) = 0;
    virtual std::optional<library::PlaylistId>
    destination(std::span<const PlaylistChoice> choices) = 0;
};

class NativePlayerDialogs final : public PlayerDialogs {
public:
    explicit NativePlayerDialogs(QWidget& parent)
        : parent_{&parent}
    {
    }

    std::vector<std::filesystem::path> tracks(std::span<const std::string> extensions) override;
    std::optional<std::filesystem::path> folder() override;
    std::optional<std::filesystem::path> file(PlayerFileChoice purpose) override;
    std::optional<library::PlaylistId>
    destination(std::span<const PlaylistChoice> choices) override;

private:
    QPointer<QWidget> parent_;
};

} // namespace wiola::gui
