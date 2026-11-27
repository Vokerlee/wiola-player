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

#include "player_dialogs.hpp"

#include <wiola/qt/paths.hpp>

#include <QCursor>
#include <QFileDialog>
#include <QMenu>

#include <map>

namespace wiola::gui {
namespace {

std::optional<std::filesystem::path> path_choice(const QString& value)
{
    return value.isEmpty() ? std::nullopt
                           : std::optional{platform::qt::QtPathAdapter::from_qt(value)};
}

} // namespace

std::vector<std::filesystem::path>
NativePlayerDialogs::tracks(std::span<const std::string> extensions)
{
    if (!parent_)
        return {};

    QStringList patterns;

    for (const auto& extension : extensions)
        patterns.append(QString::fromStdString("*" + extension));

    const auto chosen = QFileDialog::getOpenFileNames(parent_, "Choose tracks", {},
        QString{"Audio (%1);;All files (*)"}.arg(patterns.join(' ')));
    std::vector<std::filesystem::path> paths;

    for (const auto& value : chosen)
        paths.push_back(platform::qt::QtPathAdapter::from_qt(value));

    return paths;
}

std::optional<std::filesystem::path> NativePlayerDialogs::folder()
{
    return parent_
        ? path_choice(QFileDialog::getExistingDirectory(parent_, "Open folder workspace"))
        : std::nullopt;
}

FileDialogSpecification file_dialog_specification(PlayerFileChoice purpose)
{
    switch (purpose) {
    case PlayerFileChoice::import_playlist:
        return {"Import playlist", "M3U8 playlists (*.m3u8 *.m3u);;All files (*)", false};
    case PlayerFileChoice::export_playlist:
        return {"Save playlist as", "M3U8 playlists (*.m3u8)", true};
    case PlayerFileChoice::import_layout:
        return {"Import layout", "Layout (*.json)", false};
    case PlayerFileChoice::export_layout:
        return {"Export layout", "Layout (*.json)", true};
    case PlayerFileChoice::import_theme:
        return {"Import theme", "Theme (*.json)", false};
    case PlayerFileChoice::export_theme:
        return {"Export theme", "Theme (*.json)", true};
    }
    return {};
}

std::optional<std::filesystem::path> NativePlayerDialogs::file(PlayerFileChoice purpose)
{
    if (!parent_)
        return {};

    const auto [title, filter, save] = file_dialog_specification(purpose);

    if (title.isEmpty())
        return {};

    return path_choice(save ? QFileDialog::getSaveFileName(parent_, title, {}, filter)
                            : QFileDialog::getOpenFileName(parent_, title, {}, filter));
}

std::optional<library::PlaylistId>
NativePlayerDialogs::destination(std::span<const PlaylistChoice> choices)
{
    if (!parent_)
        return {};

    // A local menu is independent of the owner if a nested event loop closes the window.
    QMenu menu;
    std::map<QAction*, library::PlaylistId> targets;

    for (const auto& choice : choices)
        targets.emplace(menu.addAction(QString::fromStdString(choice.name)), choice.id);

    if (choices.empty())
        menu.addAction("No saved playlists")->setEnabled(false);

    const auto* selected = menu.exec(QCursor::pos());

    for (const auto& [action, id] : targets)
        if (action == selected)
            return id;

    return {};
}

} // namespace wiola::gui
