/**
 * @file
 * @brief Generation-aware workspace publication regression tests.
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

#include <desktop/workspace/workspace_scan_publisher.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <functional>

namespace {

void file(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path} << "audio";
}

bool pump_until(std::function<bool()> predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};

    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }

    return predicate();
}

TEST(WorkspaceScanPublisher, PublishesIncrementalBatchesOnTheGuiThread)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "wiola-publish";
    std::filesystem::remove_all(root);
    for (int index = 0; index < 70; ++index)
        file(root / (std::to_string(index) + ".wav"));

    std::vector<wiola::app::WorkspaceScanBatch> batches;
    std::vector<std::filesystem::path> tracks;
    wiola::app::WorkspaceScanPublisher publisher{*QCoreApplication::instance(),
        [&](wiola::app::WorkspaceScanBatch batch) {
            EXPECT_EQ(QThread::currentThread(), QCoreApplication::instance()->thread());
            for (const auto& entry : batch.entries)
                tracks.push_back(entry.location);
            batches.push_back(std::move(batch));
        }};
    const auto generation = publisher.start(
        {.id = wiola::library::WorkspaceId{"workspace"}, .root = root, .recursive = true}, {".wav"}
    );

    ASSERT_TRUE(pump_until([&] {
        return !batches.empty() &&
            batches.back().status != wiola::app::WorkspaceScanStatus::running;
    }));
    EXPECT_EQ(batches.back().status, wiola::app::WorkspaceScanStatus::complete);
    EXPECT_EQ(tracks.size(), 70);
    EXPECT_TRUE(std::ranges::is_sorted(tracks));
    EXPECT_EQ(batches.back().generation, generation);

    for (const auto& batch : batches)
        EXPECT_LE(batch.entries.size(), 64);

    std::filesystem::remove_all(root);
}

TEST(WorkspaceScanPublisher, DropsQueuedResultsFromAnOlderFolder)
{
    const std::filesystem::path base = std::filesystem::temp_directory_path() / "wiola-generations";
    const std::filesystem::path old_root = base / "old";
    const std::filesystem::path new_root = base / "new";
    std::filesystem::remove_all(base);
    file(old_root / "old.wav");
    file(new_root / "new.wav");

    std::vector<wiola::app::WorkspaceScanBatch> batches;
    wiola::app::WorkspaceScanPublisher publisher{*QCoreApplication::instance(),
        [&](wiola::app::WorkspaceScanBatch batch) { batches.push_back(std::move(batch)); }};

    const auto old_generation = publisher.start(
        {.id = wiola::library::WorkspaceId{"old"}, .root = old_root, .recursive = true}, {".wav"}
    );
    const auto current = publisher.start(
        {.id = wiola::library::WorkspaceId{"current"}, .root = new_root, .recursive = true},
        {".wav"}
    );

    EXPECT_NE(old_generation, current);
    ASSERT_TRUE(pump_until([&] {
        return !batches.empty() &&
            batches.back().status != wiola::app::WorkspaceScanStatus::running;
    }));

    for (const auto& batch : batches) {
        EXPECT_EQ(batch.workspace, wiola::library::WorkspaceId{"current"});
        EXPECT_EQ(batch.generation, current);
    }

    std::filesystem::remove_all(base);
}

TEST(WorkspaceScanPublisher, CancellationInvalidatesAlreadyQueuedBatches)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "wiola-cancel-queue";
    std::filesystem::remove_all(root);
    file(root / "track.wav");

    std::vector<wiola::app::WorkspaceScanBatch> batches;
    wiola::app::WorkspaceScanPublisher publisher{*QCoreApplication::instance(),
        [&](wiola::app::WorkspaceScanBatch batch) { batches.push_back(std::move(batch)); }};

    publisher.start(
        {.id = wiola::library::WorkspaceId{"workspace"}, .root = root, .recursive = true}, {".wav"}
    );
    publisher.cancel();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    EXPECT_TRUE(batches.empty());
    std::filesystem::remove_all(root);
}

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    QCoreApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
