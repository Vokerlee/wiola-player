/**
 * @file
 * @brief Qt storage driver for the portable snapshot-writing contract.
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

#include "snapshot_writer.hpp"
#include "library_store.hpp"

namespace wiola::persistence::qt {

app::SnapshotWriteResult snapshot_write_result(LibraryStoreResult result) noexcept
{
    using Error = app::SnapshotWriteError;
    if (result.io == FileStoreError::too_large)
        return {Error::too_large};
    if (result.io != FileStoreError::none)
        return {Error::io};
    switch (result.schema) {
    case LibraryJsonError::none:
        return {};
    case LibraryJsonError::unsupported_version:
        return {Error::unsupported_version};
    case LibraryJsonError::too_large:
    case LibraryJsonError::limit_exceeded:
        return {Error::too_large};
    case LibraryJsonError::malformed:
    case LibraryJsonError::invalid_record:
        return {Error::invalid_snapshot};
    }
    return {Error::invalid_snapshot};
}

app::SnapshotWriteResult SnapshotWriter::write(const library::LibrarySnapshot& snapshot)
{
    return snapshot_write_result(store_.save(snapshot));
}

} // namespace wiola::persistence::qt
