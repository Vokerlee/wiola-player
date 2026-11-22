/**
 * @file
 * @brief Debounced workspace filesystem watcher regression tests.
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

#include <desktop/workspace/workspace_watcher.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <gtest/gtest.h>

#include <fstream>

namespace {

bool pump_until(std::function<bool()> predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};

    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }

    return predicate();
}

TEST(WorkspaceWatcher, CoalescesFilesystemBurstsAndStopsAfterUnwatch)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "wiola-watch";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    wiola::app::WorkspaceWatcher watcher;
    const wiola::library::WorkspaceRecord workspace{.id = wiola::library::WorkspaceId{"workspace"},
        .root = root,
        .recursive = true};
    ASSERT_TRUE(watcher.watch(workspace));

    int notifications{0};
    watcher.on_changed([&](wiola::library::WorkspaceId id) {
        EXPECT_EQ(id, workspace.id);
        ++notifications;
    });

    std::ofstream{root / "first.wav"} << "first";
    ASSERT_TRUE(pump_until([&] { return notifications == 1; }));

    for (int index = 0; index < 5; ++index)
        std::ofstream{root / ("burst-" + std::to_string(index) + ".wav")} << "audio";

    ASSERT_TRUE(pump_until([&] { return notifications == 2; }));
    QThread::msleep(250);
    QCoreApplication::processEvents();
    EXPECT_EQ(notifications, 2);

    watcher.unwatch(workspace.id);
    std::ofstream{root / "ignored.wav"} << "ignored";
    QThread::msleep(250);
    QCoreApplication::processEvents();
    EXPECT_EQ(notifications, 2);

    EXPECT_FALSE(watcher.watch(
        {.id = wiola::library::WorkspaceId{"missing"}, .root = root / "missing", .recursive = true}
    ));

    std::filesystem::remove_all(root);
}

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    QCoreApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
