/**
 * @file
 * @brief Native semantic theme regression tests.
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
#include <persistence/qt/atomic_file_store.hpp>
#include <qt/paths.hpp>

namespace wiola::testing {

class FaultAtomicOutput final : public persistence::qt::AtomicOutput {
public:
    FaultAtomicOutput(const std::filesystem::path& path, bool& fail,
        std::function<void()> hook = {})
        : file_{platform::qt::QtPathAdapter::to_qt(path)}
        , fail_{fail}
        , hook_{std::move(hook)}
    {
        opened_ = file_.open(QIODevice::WriteOnly);
    }

    qint64 write(QByteArrayView bytes) override
    {
        return opened_ ? file_.write(bytes.data(), bytes.size()) : -1;
    }

    bool commit() override
    {
        if (hook_) {
            auto notify = std::move(hook_);
            notify();
        }
        return !fail_ && opened_ && file_.commit();
    }

private:
    QSaveFile file_;
    bool& fail_;
    bool opened_{};
    std::function<void()> hook_;
};

} // namespace wiola::testing
