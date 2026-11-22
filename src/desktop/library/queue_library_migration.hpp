/**
 * @file
 * @brief Non-destructive migration of the QSettings queue into the library.
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

#include <persistence/qt/library_json.hpp>

#include <QSettings>

#include <filesystem>

namespace wiola::app {

class QueueLibraryMigration {
public:
    /// Uses a `library.json` beside the supplied settings file.
    explicit QueueLibraryMigration(QSettings& settings);

    /// Preserves an existing valid library; otherwise imports the queue without deleting it.
    persistence::qt::LibraryJsonError migrate(QSettings& settings) const;

private:
    std::filesystem::path file_;
};

} // namespace wiola::app
