/**
 * @file
 * @brief Atomic, coalesced, backup-recoverable library persistence.
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

#include "library_storage.hpp"

#include <platform/qt/qt_path_adapter.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <chrono>
#include <utility>

namespace wiola::gui {

namespace {

struct Candidate {
    QByteArray json;
    LibraryJsonError error{LibraryJsonError::none};
    bool exists{false};
};

Candidate read_candidate(const std::filesystem::path& path)
{
    Candidate candidate;
    const QString name{platform::qt::QtPathAdapter::to_qt(path)};

    candidate.exists = QFile::exists(name);

    if (!candidate.exists)
        return candidate;

    QFile file{name};

    if (!file.open(QIODevice::ReadOnly)) {
        candidate.error = LibraryJsonError::io;
        return candidate;
    }

    candidate.json = file.readAll();

    if (file.error() != QFileDevice::NoError)
        candidate.error = LibraryJsonError::io;

    return candidate;
}

LibraryJsonError write_atomic(const std::filesystem::path& path, const QByteArray& json)
{
    QSaveFile file{platform::qt::QtPathAdapter::to_qt(path)};

    if (!file.open(QIODevice::WriteOnly))
        return LibraryJsonError::io;

    if (file.write(json) != json.size())
        return LibraryJsonError::io;

    return file.commit() ? LibraryJsonError::none : LibraryJsonError::io;
}

} // namespace

LibraryStorage::LibraryStorage(std::filesystem::path file, QObject* parent)
    : QObject{parent}
    , file_{std::move(file)}
    , backup_{file_}
{
    backup_ += ".bak";

    coalesce_.setSingleShot(true);
    coalesce_.setInterval(std::chrono::milliseconds{50});

    connect(&coalesce_, &QTimer::timeout, this, [this] {
        const LibraryJsonError error{flush()};

        if (error != LibraryJsonError::none && failure_observer_)
            failure_observer_(error);
    });
}

LibraryStorage::~LibraryStorage()
{
    static_cast<void>(flush());
}

LibraryJsonError LibraryStorage::load(LibrarySnapshot& output) const
{
    const Candidate primary{read_candidate(file_)};

    if (primary.exists && primary.error == LibraryJsonError::none) {
        const LibraryJsonError error{read_library_json(primary.json, output)};

        if (error == LibraryJsonError::none)
            return error;
    }

    const Candidate backup{read_candidate(backup_)};

    if (backup.exists && backup.error == LibraryJsonError::none) {
        const LibraryJsonError error{read_library_json(backup.json, output)};

        if (error == LibraryJsonError::none)
            return error;
    }

    if (primary.error == LibraryJsonError::io || backup.error == LibraryJsonError::io)
        return LibraryJsonError::io;

    if (primary.exists)
        return read_library_json(primary.json, output);

    if (backup.exists)
        return read_library_json(backup.json, output);

    output = {};

    return LibraryJsonError::none;
}

LibraryJsonError LibraryStorage::save(const LibrarySnapshot& snapshot)
{
    coalesce_.stop();
    pending_.reset();

    return write(snapshot);
}

void LibraryStorage::schedule(LibrarySnapshot snapshot)
{
    pending_ = std::move(snapshot);
    coalesce_.start();
}

LibraryJsonError LibraryStorage::flush()
{
    coalesce_.stop();

    if (!pending_)
        return LibraryJsonError::none;

    const LibraryJsonError error{write(*pending_)};

    if (error == LibraryJsonError::none)
        pending_.reset();

    return error;
}

void LibraryStorage::on_save_failed(std::function<void(LibraryJsonError)> observer)
{
    failure_observer_ = std::move(observer);
}

LibraryJsonError LibraryStorage::write(const LibrarySnapshot& snapshot)
{
    QByteArray json;
    const LibraryJsonError serialized{write_library_json(snapshot, json)};

    if (serialized != LibraryJsonError::none)
        return serialized;

    const QString name{platform::qt::QtPathAdapter::to_qt(file_)};

    if (!QDir{}.mkpath(QFileInfo{name}.absolutePath()))
        return LibraryJsonError::io;

    const Candidate current{read_candidate(file_)};

    if (current.error == LibraryJsonError::io)
        return current.error;

    if (current.exists) {
        LibrarySnapshot previous;

        if (read_library_json(current.json, previous) == LibraryJsonError::none) {
            const LibraryJsonError backed_up{write_atomic(backup_, current.json)};

            if (backed_up != LibraryJsonError::none)
                return backed_up;
        }
    }

    return write_atomic(file_, json);
}

} // namespace wiola::gui
