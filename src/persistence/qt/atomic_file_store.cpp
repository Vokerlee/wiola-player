/**
 * @file
 * @brief Persistent track, metadata, tag, and playlist record shapes.
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

#include "atomic_file_store.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <limits>
#include <platform/qt/qt_path_adapter.hpp>
#include <stdexcept>
#include <utility>

namespace wiola::persistence::qt {
namespace {

class QtAtomicOutput final : public AtomicOutput {
public:
    explicit QtAtomicOutput(const QString& path)
        : file_{path}
    {
        opened_ = file_.open(QIODevice::WriteOnly);
    }

    qint64 write(QByteArrayView bytes) override
    {
        return opened_ ? file_.write(bytes.data(), bytes.size()) : -1;
    }

    bool commit() override { return opened_ && file_.commit(); }

private:
    QSaveFile file_;
    bool opened_{false};
};

} // namespace

AtomicFileStore::AtomicFileStore(std::filesystem::path file, qsizetype max_bytes,
    OutputFactory output)
    : file_{std::move(file)}
    , max_bytes_{max_bytes}
    , output_{std::move(output)}
{
    if (max_bytes < 0 || max_bytes == std::numeric_limits<qsizetype>::max())
        throw std::invalid_argument{"invalid file size limit"};
    if (!output_)
        output_ = [](const std::filesystem::path& path) {
            return std::make_unique<QtAtomicOutput>(platform::qt::QtPathAdapter::to_qt(path));
        };
}

std::filesystem::path AtomicFileStore::path(FileSlot slot) const
{
    auto result = file_;
    if (slot == FileSlot::backup)
        result += ".bak";
    return result;
}

FileReadResult AtomicFileStore::read(FileSlot slot) const
{
    QFile file{platform::qt::QtPathAdapter::to_qt(path(slot))};
    FileReadResult result;
    result.exists = file.exists();
    if (!result.exists)
        return result;
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = FileStoreError::io;
        return result;
    }
    if (file.size() > max_bytes_) {
        result.error = FileStoreError::too_large;
        return result;
    }
    result.bytes = file.read(max_bytes_ + 1);
    if (file.error() != QFileDevice::NoError)
        result.error = FileStoreError::io;
    else if (result.bytes.size() > max_bytes_)
        result.error = FileStoreError::too_large;
    if (result.error != FileStoreError::none)
        result.bytes.clear();
    return result;
}

FileStoreError AtomicFileStore::write(FileSlot slot, QByteArrayView bytes)
{
    if (bytes.size() > max_bytes_)
        return FileStoreError::too_large;
    const auto target = path(slot);
    if (!QDir{}.mkpath(QFileInfo{platform::qt::QtPathAdapter::to_qt(target)}.absolutePath()))
        return FileStoreError::io;
    auto output = output_(target);
    if (!output || output->write(bytes) != bytes.size() || !output->commit())
        return FileStoreError::io;
    return FileStoreError::none;
}

} // namespace wiola::persistence::qt
