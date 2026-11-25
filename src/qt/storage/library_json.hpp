/**
 * @file
 * @brief Bounded versioned JSON for persistent library records.
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

#include <QtGlobal>
#include <wiola/library/snapshot.hpp>

#include <vector>

class QByteArray;
class QByteArrayView;

namespace wiola::persistence::qt {

inline constexpr qsizetype max_library_document_bytes{16 * 1024 * 1024};

enum class LibraryJsonError {
    none,
    too_large,
    malformed,
    unsupported_version,
    invalid_record,
    limit_exceeded,
};

[[nodiscard]] LibraryJsonError write_library_json(const library::LibrarySnapshot& library,
    QByteArray& output);
[[nodiscard]] LibraryJsonError read_library_json(QByteArrayView json,
    library::LibrarySnapshot& output);

} // namespace wiola::persistence::qt
