/**
 * @file
 * @brief Repository rollback, allocation, and persistence contract tests.
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
#include <QSaveFile>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <persistence/qt/atomic_file_store.hpp>
#include <qt/paths.hpp>
#include <thread>

namespace wiola::testing {

struct AtomicWriteGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered{}, released{};
    std::atomic<bool> fail{};
    std::atomic<int> primary_writes{};
    std::thread::id writer;

    bool wait_entered()
    {
        std::unique_lock lock{mutex};
        return changed.wait_for(lock, std::chrono::seconds{10}, [&] { return entered; });
    }

    void release()
    {
        std::lock_guard lock{mutex};
        released = true;
        changed.notify_all();
    }

    ~AtomicWriteGate() { release(); }
};

class GatedAtomicOutput final : public persistence::qt::AtomicOutput {
public:
    GatedAtomicOutput(const std::filesystem::path& path, const std::filesystem::path& primary,
        AtomicWriteGate& gate)
        : file_{platform::qt::QtPathAdapter::to_qt(path)}
        , gate_{gate}
        , primary_{path == primary}
        , opened_{file_.open(QIODevice::WriteOnly)}
    {
    }

    qint64 write(QByteArrayView bytes) override
    {
        return opened_ ? file_.write(bytes.data(), bytes.size()) : -1;
    }

    bool commit() override
    {
        if (primary_) {
            std::unique_lock lock{gate_.mutex};
            gate_.writer = std::this_thread::get_id();
            ++gate_.primary_writes;
            gate_.entered = true;
            gate_.changed.notify_all();
            gate_.changed.wait(lock, [&] { return gate_.released; });
        }
        return opened_ && !gate_.fail.load() && file_.commit();
    }

private:
    QSaveFile file_;
    AtomicWriteGate& gate_;
    bool primary_, opened_;
};

} // namespace wiola::testing
