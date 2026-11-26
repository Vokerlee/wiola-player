/**
 * @file
 * @brief The single native-Unicode conversion boundary between Qt and filesystem paths.
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

#include <QDir>
#include <QString>

#include <filesystem>

namespace wiola::platform::qt::QtPathAdapter {

[[nodiscard]] inline std::filesystem::path from_qt(const QString& path)
{
#ifdef _WIN32
    return std::filesystem::path{path.toStdU16String()};
#else
    return std::filesystem::path{path.toStdString()};
#endif
}

[[nodiscard]] inline QString to_qt(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdU16String(path.u16string());
#else
    return QString::fromStdString(path.string());
#endif
}

/// Native UI spelling; never use display text as a catalog identity or stored location.
[[nodiscard]] inline QString display(const std::filesystem::path& path)
{
    auto text = QDir::toNativeSeparators(to_qt(path));
#ifdef _WIN32
    if (text.size() >= 2 && text.at(1) == u':')
        text[0] = text.at(0).toUpper();
#endif
    return text;
}

} // namespace wiola::platform::qt::QtPathAdapter
