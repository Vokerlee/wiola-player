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

#pragma once
#include <QByteArray>
#include <QByteArrayView>
#include <filesystem>
#include <functional>
#include <memory>

namespace wiola::persistence::qt {

enum class FileStoreError {
    none,
    io,
    too_large,
};
enum class FileSlot {
    primary,
    backup,
};

struct FileReadResult {
    QByteArray bytes;
    FileStoreError error{FileStoreError::none};
    bool exists{false};
};

/// An uncommitted atomic output; destruction discards incomplete output.
class AtomicOutput {
public:
    virtual ~AtomicOutput() = default;
    virtual qint64 write(QByteArrayView bytes) = 0;
    virtual bool commit() = 0;
};

class AtomicFileStore final {
public:
    using OutputFactory =
        std::function<std::unique_ptr<AtomicOutput>(const std::filesystem::path&)>;
    AtomicFileStore(std::filesystem::path file, qsizetype max_bytes, OutputFactory output = {});
    [[nodiscard]] FileReadResult read(FileSlot slot) const;
    [[nodiscard]] FileStoreError write(FileSlot slot, QByteArrayView bytes);

private:
    [[nodiscard]] std::filesystem::path path(FileSlot slot) const;
    std::filesystem::path file_;
    qsizetype max_bytes_;
    OutputFactory output_;
};

} // namespace wiola::persistence::qt
