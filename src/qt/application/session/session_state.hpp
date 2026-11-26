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

#pragma once
#include <QByteArray>
#include <QRect>
#include <QString>
#include <map>
#include <presentation/documents/session_documents.hpp>

namespace wiola::app {

struct SessionState {
    gui::SessionDocuments documents;
    QRect window;
    std::map<std::string, std::vector<int>> splitters;
    QString layout_identity;
    bool operator==(const SessionState&) const = default;
};
enum class SessionStateError {
    none,
    invalid,
    unsupported_version,
    too_large,
    io,
    busy,
};
[[nodiscard]] QByteArray write_session_state(const SessionState& state);
[[nodiscard]] SessionStateError read_session_state(QByteArrayView bytes, SessionState& output);

} // namespace wiola::app
